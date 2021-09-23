#include "db.h"

uint32_t* node_parent(void* node) { return node + PARENT_POINTER_OFFSET; }

NodeType get_node_type(void* node) {
	uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSIZE));
	return (NodeType)value;
}

void set_node_type(void* node, NodeType type) {
	uint8_t value = type;
	*((uint8_t*)(node + NODE_TYPE_OFFSIZE)) = value;
}

bool is_node_root(void* node) {
	uint8_t value = *((uint8_t*)(node + IS_ROOT_OFFSET));
	return (bool)value;
}

void set_node_root(void* node, bool is_root) {
	uint8_t value = is_root;
	*((uint8_t*)(node + IS_ROOT_OFFSET)) = value;
}

uint32_t get_node_max_key(void* node) {
	switch (get_node_type(node)) {
		case NODE_INTERNAL:
			return *internal_node_key(node, *internal_node_num_keys(node) - 1);
		case NODE_LEAF:
			return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
	}
}


void initialize_leaf_node(void* node) {
	set_node_type(node, NODE_LEAF);
	set_node_root(node, false);
	*leaf_node_num_cells(node) = 0;
	*leaf_node_next_leaf(node) = 0; // 0 represents no sibling
}

uint32_t* leaf_node_num_cells(void* node) {
	return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
	return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
	return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
	return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

uint32_t* leaf_node_next_leaf(void* node) {
	return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}


void initialize_internal_node(void* node) {
	set_node_type(node, NODE_INTERNAL);
	set_node_root(node, false);
	*internal_node_num_keys(node) = 0;
}

uint32_t* internal_node_num_keys(void* node) {
	return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t* internal_node_right_child(void* node) {
	return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
	return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t* internal_node_key(void* node, uint32_t key_num) {
	return (void*)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

uint32_t* internal_node_child(void* node, uint32_t child_num) {
	uint32_t num_keys = *internal_node_num_keys(node);
	if (child_num > num_keys) {
		printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
		exit(EXIT_FAILURE);
	} else if (child_num == num_keys) {
		return internal_node_right_child(node);
	} else {
		return internal_node_cell(node, child_num);
	}
}
