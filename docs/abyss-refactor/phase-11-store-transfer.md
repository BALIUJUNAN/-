# Phase 11: Store Inventory and Transfer

## Scope

Phase 11 moves `Store`, `StoreStock`, `TransferOrder`, and `TransferItem` to
AbyssDB. The old `store.txt`, `store_stock.txt`, `transfer.txt`, and
`transfer_item.txt` files are read-only migration inputs.

## Storage

- `0xa3`: store primary records.
- `0xa4`: unique store-name index.
- `0xa5`: store/product inventory records.
- `0xa6`: transfer primary records.
- `0xa7`: transfer items grouped by transfer ID.
- `0xa8`: transfer status index.
- `0xa9`: source/destination store index.

Values use versioned TLV codecs. IDs use persistent counters. Store stock uses
the composite `(store_id, product_id)` primary key and cannot become negative.

## Transfer Protocol

1. A draft can receive items only before approval.
2. Approval requires at least one item.
3. Outbound confirmation validates all source quantities and marks the order
   in transit. It does not partially mutate inventory.
4. Inbound confirmation deducts every source quantity, adds every destination
   quantity, changes the order state, and appends the audit record in one UOW.
5. Any missing item or insufficient quantity rolls back the complete UOW.

This preserves the legacy workflow while making the physical inventory move a
single atomic operation.

## Migration

Migration marker `41 0b` is written only after all four source files are
parsed, duplicate append records are collapsed, references are validated, and
the import transaction commits. A failed import can be retried after repairing
the source data.

## Acceptance

- Store and transfer CRUD survives reopen.
- Status and store queries use their namespace indexes.
- A failed transfer leaves both stores and the order unchanged.
- Runtime public APIs no longer write the four legacy files.

