/**
 * @file hash.h
 * @brief 哈希表数据结构与操作
 */
#ifndef HASH_H
#define HASH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

// SHA-256
#define SHA256_BLOCK_SIZE 32
typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[SHA256_BLOCK_SIZE]);
void sha256_hash_to_hex(const uint8_t hash[SHA256_BLOCK_SIZE], char hex[65]);

#endif // HASH_H
