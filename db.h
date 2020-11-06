#ifndef __db_h__
#define __db_h__

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

typedef struct {
	uint32_t id;
	char username[COLUMN_USERNAME_SIZE + 1];
	char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ID_SIZE;
const uint32_t USERNAME_SIZE;
const uint32_t EMAIL_SIZE;
const uint32_t ID_OFFSET;
const uint32_t USERNAME_OFFSET;
const uint32_t EMAIL_OFFSET;
const uint32_t ROW_SIZE;

void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);


#define TABLE_MAX_PAGES 100
const uint32_t PAGE_SIZE;
const uint32_t ROWS_PER_PAGE;
const uint32_t TABLE_MAX_ROWS;


typedef struct {
	int file_descriptor;
	uint32_t file_length;
	uint32_t num_pages;
	void* pages[TABLE_MAX_PAGES];
} Pager;

Pager* pager_open(const char* filename);
void pager_flush(Pager* pager, uint32_t page_num);
void* get_page(Pager* pager, uint32_t page_num);
uint32_t get_unused_page_num(Pager* pager);


typedef struct {
	Pager* pager;
	uint32_t root_page_num;
} Table;

Table* db_open(const char* filename);
void db_close(Table* table);


typedef struct {
	Table* table;
	uint32_t page_num;
	uint32_t cell_num;
	bool end_of_table; // Indicates a position one past the last element
} Cursor;

Cursor* table_start(Table* table);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);

Cursor* table_find(Table* table, uint32_t key);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key);
uint32_t internal_node_find_child(void* node, uint32_t key);

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value);


typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

// Common Node Header Layout
const uint32_t NODE_TYPE_SIZE;
const uint32_t NODE_TYPE_OFFSIZE;
const uint32_t IS_ROOT_SIZE;
const uint32_t IS_ROOT_OFFSET;
const uint32_t PARENT_POINTER_SIZE;
const uint32_t PARENT_POINTER_OFFSET;
const uint8_t COMMON_NODE_HEADER_SIZE;

// Leaf Node Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET;
const uint32_t LEAF_NODE_HEADER_SIZE;

// Leaf Node Body Layout
const uint32_t LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_KEY_OFFSET;
const uint32_t LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET;
const uint32_t LEAF_NODE_CELL_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS;
const uint32_t LEAF_NODE_MAX_CELLS;

const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT;

// Internal Node Header Layout
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET;
const uint32_t INTERNAL_NODE_HEADER_SIZE;

// Internal Node Body Layout
const uint32_t INTERNAL_NODE_CHILD_SIZE;
const uint32_t INTERNAL_NODE_KEY_SIZE;
const uint32_t INTERNAL_NODE_CELL_SIZE;
// Keep this small for testing
const uint32_t INTERNAL_NODE_MAX_CELLS;

// helper function
uint32_t* node_parent(void* node);
NodeType get_node_type(void* node);
void set_node_type(void* node, NodeType type);
bool is_node_root(void* node);
void set_node_root(void* node, bool is_root);
uint32_t get_node_max_key(void* node);

void initialize_leaf_node(void* node);
uint32_t* leaf_node_num_cells(void* node);
void* leaf_node_cell(void* node, uint32_t cell_num);
uint32_t* leaf_node_key(void* node, uint32_t cell_num);
void* leaf_node_value(void* node, uint32_t cell_num);
uint32_t* leaf_node_next_leaf(void* node);

void initialize_internal_node(void* node);
uint32_t* internal_node_num_keys(void* node);
uint32_t* internal_node_right_child(void* node);
uint32_t* internal_node_cell(void* node, uint32_t cell_num);
uint32_t* internal_node_key(void* node, uint32_t key_num);
uint32_t* internal_node_child(void* node, uint32_t child_num);


#endif
