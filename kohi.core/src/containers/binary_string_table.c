#include "binary_string_table.h"
#include "darray.h"
#include "debug/kassert.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

binary_string_table binary_string_table_create(void) {
    binary_string_table table = {
        .lookup = darray_create(binary_string_table_entry),
        .header.entry_count = 0,
        .header.data_block_size = 0,
        .data = 0};

    return table;
}

binary_string_table binary_string_table_from_block(void* block) {
    KASSERT_DEBUG(block);

    binary_string_table* temp = (binary_string_table*)block;
    // The lookup is the block of memory immediately after the header.
    u64 lookup_offset = sizeof(binary_string_table_header);
    temp->lookup = (binary_string_table_entry*)(((u8*)block) + lookup_offset);
    // Data block comes after the lookup array
    u64 data_offset = lookup_offset + (sizeof(binary_string_table_entry) * temp->header.entry_count);
    temp->data = (char*)(((u8*)block) + data_offset);

    binary_string_table table = binary_string_table_create();

    table.header.entry_count = temp->header.entry_count;
    table.header.data_block_size = temp->header.data_block_size;

    table.lookup = darray_reserve(binary_string_table_entry, table.header.entry_count);
    // Copy the temp array into the new darray.
    KCOPY_TYPE_CARRAY(table.lookup, temp->lookup, binary_string_table_entry, table.header.entry_count);
    // Ensure its length is set as well.
    darray_length_set(table.lookup, table.header.entry_count);

    // Take a copy of the incoming data.
    table.data = kallocate(table.header.data_block_size, MEMORY_TAG_BINARY_STRING_TABLE);
    kcopy_memory(table.data, temp->data, table.header.data_block_size);

    return table;
}

void binary_string_table_destroy(binary_string_table* table) {
    KASSERT_DEBUG(table);

    darray_destroy(table->lookup);

    if (table->data) {
        kfree(table->data, table->header.data_block_size, MEMORY_TAG_BINARY_STRING_TABLE);
    }

    kzero_memory(table, sizeof(binary_string_table));
}

u32 binary_string_table_add(binary_string_table* table, const char* string) {
    KASSERT_DEBUG(table);
    KASSERT_DEBUG(string && string_length(string) > 0);

    binary_string_table_entry new_entry = {
        .length = string_length(string),
        .offset = table->header.data_block_size};

    char* new_db = kallocate(table->header.data_block_size + new_entry.length, MEMORY_TAG_BINARY_STRING_TABLE);

    if (table->data && table->header.entry_count) {
        kcopy_memory(new_db, table->data, table->header.data_block_size);
        kfree(table->data, table->header.data_block_size, MEMORY_TAG_BINARY_STRING_TABLE);
    }

    table->data = new_db;

    // Copy the string's content.
    kcopy_memory(table->data + new_entry.offset, string, new_entry.length);

    // Push the lookup entry.
    darray_push(table->lookup, new_entry);
    table->header.entry_count = darray_length(table->lookup);

    table->data = new_db;

    // The index is also conveniently the last entry.
    return table->header.entry_count - 1;
}

const char* binary_string_table_get(const binary_string_table* table, u32 index) {
    KASSERT_DEBUG(table);
    KASSERT_DEBUG(index < table->header.entry_count);

    binary_string_table_entry* entry = &table->lookup[index];

    char* out_str = kallocate(entry->length + 1, MEMORY_TAG_STRING);
    kcopy_memory(out_str, table->data + entry->offset, entry->length);

    return (const char*)out_str;
}

u32 binary_string_table_length_get(const binary_string_table* table, u32 index) {
    KASSERT_DEBUG(table);
    KASSERT_DEBUG(index < table->header.entry_count);
    return table->lookup[index].length;
}

void binary_string_table_get_buffered(const binary_string_table* table, u32 index, char* buffer) {
    KASSERT_DEBUG(table);
    KASSERT_DEBUG(buffer);
    KASSERT_DEBUG(index < table->header.entry_count);

    binary_string_table_entry* entry = &table->lookup[index];

    kcopy_memory(buffer, table->data + entry->offset, entry->length);
}

void* binary_string_table_serialized(const binary_string_table* table, u64* out_size) {
    u64 total_size = sizeof(binary_string_table_header) + (sizeof(binary_string_table_entry) * table->header.entry_count) + table->header.data_block_size;

    void* out_block = kallocate(total_size, MEMORY_TAG_BINARY_DATA);
    KCOPY_TYPE(out_block, &table->header, binary_string_table_header);

    u64 lookup_offset = sizeof(binary_string_table_header);
    KCOPY_TYPE_CARRAY((void*)(((u8*)out_block) + lookup_offset), table->lookup, binary_string_table_entry, table->header.entry_count);

    u64 data_offset = lookup_offset + (sizeof(binary_string_table_entry) * table->header.entry_count);
    kcopy_memory((void*)(((u8*)out_block) + data_offset), table->data, table->header.data_block_size);

    *out_size = total_size;
    return out_block;
}
