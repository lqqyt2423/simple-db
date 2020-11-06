#include "db.h"

void create_new_root(Table* table, uint32_t right_child_page_num);
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key);
void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);

Table* db_open(const char* filename) {
	Pager* pager = pager_open(filename);

	Table* table = malloc(sizeof(Table));
	table->pager = pager;
	table->root_page_num = 0;

	if (pager->num_pages == 0) {
		// New database file. Initialize page 0 as leaf node.
		void* root_node = get_page(pager, 0);
		initialize_leaf_node(root_node);
		set_node_root(root_node, true);
	}

	return table;
}

void db_close(Table* table) {
	Pager* pager = table->pager;

	for (uint32_t i = 0; i < pager->num_pages; i++) {
		if (pager->pages[i] == NULL) {
			continue;
		}
		pager_flush(pager, i);
		free(pager->pages[i]);
		pager->pages[i] = NULL;
	}

	int result = close(pager->file_descriptor);
	if (result == -1) {
		printf("Error closing db file.\n");
		exit(EXIT_FAILURE);
	}
	for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
		void* page = pager->pages[i];
		if (page) {
			free(page);
			pager->pages[i] = NULL;
		}
	}
	free(pager);
	free(table);
}



Cursor* table_start(Table* table) {
	Cursor* cursor = table_find(table, 0);

	void* node = get_page(table->pager, cursor->page_num);
	uint32_t num_cells = *leaf_node_num_cells(node);
	cursor->end_of_table = (num_cells == 0);

	return cursor;
}

void* cursor_value(Cursor* cursor) {
	uint32_t page_num = cursor->page_num;
	void* page = get_page(cursor->table->pager, page_num);
	return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
	uint32_t page_num = cursor->page_num;
	void* node = get_page(cursor->table->pager, page_num);

	cursor->cell_num += 1;
	if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
		// Advance to next leaf node
		uint32_t next_page_num = *leaf_node_next_leaf(node);
		if (next_page_num == 0) {
			// This was rightmost leaf
			cursor->end_of_table = true;
		} else {
			cursor->page_num = next_page_num;
			cursor->cell_num = 0;
		}
	}
}



// Return the position of the given key.
// If the key is not present, return the position where it should be inserted
Cursor* table_find(Table* table, uint32_t key) {
	uint32_t root_page_num = table->root_page_num;
	void* root_node = get_page(table->pager, root_page_num);

	if (get_node_type(root_node) == NODE_LEAF) {
		return leaf_node_find(table, root_page_num, key);
	} else {
		return internal_node_find(table, root_page_num, key);
	}
}

Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
	void* node = get_page(table->pager, page_num);
	uint32_t num_cells = *leaf_node_num_cells(node);

	Cursor* cursor = malloc(sizeof(Cursor));
	cursor->table = table;
	cursor->page_num = page_num;

	// Binary search
	uint32_t min_index = 0;
	uint32_t one_past_max_index = num_cells;
	while (one_past_max_index != min_index) {
		uint32_t index = (min_index + one_past_max_index) / 2;
		uint32_t key_at_index = *leaf_node_key(node, index);
		if (key == key_at_index) {
			cursor->cell_num = index;
			return cursor;
		}
		if (key < key_at_index) {
			one_past_max_index = index;
		} else {
			min_index = index + 1;
		}
	}

	cursor->cell_num = min_index;
	return cursor;
}

Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
	void* node = get_page(table->pager, page_num);

	uint32_t child_index = internal_node_find_child(node, key);
	uint32_t child_num = *internal_node_child(node, child_index);
	void* child = get_page(table->pager, child_num);
	switch (get_node_type(child)) {
		case NODE_LEAF:
			return leaf_node_find(table, child_num, key);
		case NODE_INTERNAL:
			return internal_node_find(table, child_num, key);
	}
}

uint32_t internal_node_find_child(void* node, uint32_t key) {
	// Return the index of the child which should contain the given key.

	uint32_t num_keys = *internal_node_num_keys(node);

	// Binary search
	uint32_t min_index = 0;
	uint32_t max_index = num_keys; // there is one more child than key

	while (min_index != max_index) {
		uint32_t index = (min_index + max_index) / 2;
		uint32_t key_to_right = *internal_node_key(node, index);
		if (key_to_right >= key) {
			max_index = index;
		} else {
			min_index = index + 1;
		}
	}

	return min_index;
}


void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
	void* node = get_page(cursor->table->pager, cursor->page_num);

	uint32_t num_cells = *leaf_node_num_cells(node);
	if (num_cells >= LEAF_NODE_MAX_CELLS) {
		// Node full
		leaf_node_split_and_insert(cursor, key, value);
		return;
	}

	if (cursor->cell_num < num_cells) {
		// Make room for new cell
		for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
			memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
		}
	}

	*(leaf_node_num_cells(node)) += 1;
	*(leaf_node_key(node, cursor->cell_num)) = key;
	serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
	// Create a new node and move half the cells over.
	// Insert the new value in one of the two nodes.
	// Update parent or create a new parent.

	void* old_node = get_page(cursor->table->pager, cursor->page_num);
	uint32_t old_max = get_node_max_key(old_node);
	uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
	void* new_node = get_page(cursor->table->pager, new_page_num);
	initialize_leaf_node(new_node);
	*node_parent(new_node) = *node_parent(old_node);
	*leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
	*leaf_node_next_leaf(old_node) = new_page_num;

	// All existing keys plus new key should be divided
	// evenly between old (left) and new (right) nodes.
	// Starting from the right, move each key to correct position.
	for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
		void* destination_node;
		if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
			destination_node = new_node;
		} else {
			destination_node = old_node;
		}
		uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
		void* destination = leaf_node_cell(destination_node, index_within_node);

		if (i == cursor->cell_num) {
			serialize_row(value, leaf_node_value(destination_node, index_within_node));
			*leaf_node_key(destination_node, index_within_node) = key;
		} else if (i > cursor->cell_num) {
			memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
		} else {
			memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
		}
	}

	// Update cell count on both leaf nodes
	*(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
	*(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

	if (is_node_root(old_node)) {
		return create_new_root(cursor->table, new_page_num);
	} else {
		uint32_t parent_page_num = *node_parent(old_node);
		uint32_t new_max = get_node_max_key(old_node);
		void* parent = get_page(cursor->table->pager, parent_page_num);

		update_internal_node_key(parent, old_max, new_max);
		internal_node_insert(cursor->table, parent_page_num, new_page_num);
		return;
	}
}

void create_new_root(Table* table, uint32_t right_child_page_num) {
	// Handle splitting the root.
	// Old root copied to new page, becomes left child.
	// Address of right child passed in.
	// Re-initialize root page to contain the new root node.
	// New root node points to two children.

	void* root = get_page(table->pager, table->root_page_num);
	void* right_child = get_page(table->pager, right_child_page_num);
	uint32_t left_child_page_num = get_unused_page_num(table->pager);
	void* left_child = get_page(table->pager, left_child_page_num);

	// Left child has data copied from old root
	memcpy(left_child, root, PAGE_SIZE);
	set_node_root(left_child, false);

	// Root node is a new internal node with one key and two children
	initialize_internal_node(root);
	set_node_root(root, true);
	*internal_node_num_keys(root) = 1;
	*internal_node_child(root, 0) = left_child_page_num;
	uint32_t left_child_max_key = get_node_max_key(left_child);
	*internal_node_key(root, 0) = left_child_max_key;
	*internal_node_right_child(root) = right_child_page_num;
	*node_parent(left_child) = table->root_page_num;
	*node_parent(right_child) = table->root_page_num;
}

void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
	uint32_t old_child_index = internal_node_find_child(node, old_key);
	*internal_node_key(node, old_child_index) = new_key;
}

void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
	// Add a new child/key pair to parent that corresponds to child
	void* parent = get_page(table->pager, parent_page_num);
	void* child = get_page(table->pager, child_page_num);
	uint32_t child_max_key = get_node_max_key(child);
	uint32_t index = internal_node_find_child(parent, child_max_key);

	uint32_t original_num_keys = *internal_node_num_keys(parent);
	*internal_node_num_keys(parent) = original_num_keys + 1;

	if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
		printf("Need to implement splitting internal node\n");
		exit(EXIT_FAILURE);
	}

	uint32_t right_child_page_num = *internal_node_right_child(parent);
	void* right_child = get_page(table->pager, right_child_page_num);

	if (child_max_key > get_node_max_key(right_child)) {
		// Replace right child
		*internal_node_child(parent, original_num_keys) = right_child_page_num;
		*internal_node_key(parent, original_num_keys) = get_node_max_key(right_child);
		*internal_node_right_child(parent) = child_page_num;
	} else {
		// Make room for the new cell
		for (uint32_t i = original_num_keys; i > index; i--) {
			void* destination = internal_node_cell(parent, i);
			void* source = internal_node_cell(parent, i - 1);
			memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
		}
		*internal_node_child(parent, index) = child_page_num;
		*internal_node_key(parent, index) = child_max_key;
	}
}
