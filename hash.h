/**
 * @file hash.h
 * @brief 哈希表数据结构与操作
 */
#ifndef HASH_H
#define HASH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 哈希表节点
typedef struct HashNode {
    char key[64];
    void *data;
    struct HashNode *next;
} HashNode;

// 哈希表结构
typedef struct HashTable {
    HashNode **buckets;
    int size;
} HashTable;

// 哈希表操作
HashTable* hash_create(int size);
int hash_insert(HashTable *table, const char *key, void *data);
void* hash_search(HashTable *table, const char *key);
int hash_delete(HashTable *table, const char *key);
void hash_destroy(HashTable *table, void (*free_data)(void*));

#endif // HASH_H
