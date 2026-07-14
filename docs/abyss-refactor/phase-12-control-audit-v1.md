# Phase 12: Control Plane, Audit, and V1 Closure

## Scope

The final phase moves promotions, product combos, schedules, daily
settlements, and transaction logs to AbyssDB. The corresponding text files are
retained only as first-start migration inputs.

## Storage

- `0xc0-0xc2`: promotion primary, product-priority, and active-time indexes.
- `0xc3-0xc5`: combo primary, combo items, and unique barcode index.
- `0xd0-0xd1`: schedule primary and unique employee/year/week index.
- `0xd2-0xd3`: settlement primary and unique cashier/business-date index.
- `0xe0-0xe3`: audit primary, time, operator-time, and type-time indexes.

Money is persisted as signed integer cents. Discount rates and combo ratios
are persisted as integer basis points. No new value stores ABI-dependent C
struct bytes or floating-point money.

## Invariants

- One schedule exists for an employee in a given ISO year/week.
- One settlement exists for a cashier and business date.
- Settlement total and difference fields are derived from their component
  amounts and validated before every write.
- Promotion, schedule, settlement, and audit queries enter through dedicated
  namespace prefixes when the requested dimension has an index.
- Combo barcodes are unique and combo items must reference an existing combo.
- Audit writes use the same repository and can participate in business UOWs.

## Migration and Runtime

Migration marker `41 0c` covers `promotion.txt`, `combo.txt`,
`combo_item.txt`, `schedule.txt`, `daily_settlement.txt`, and
`transaction.log`. Every source is parsed before the transaction begins.
Legacy aggregate settlement columns are recomputed from cash and online
components instead of being trusted.

The terminal application's public APIs are backed by services and disposable
database-derived caches. Normal create, update, delete, load, save, report, and
backup paths do not append or rewrite these legacy files.

## V1 Completion Criteria

- The application and all phase 2-12 tests compile with the warning policy.
- Failed multi-record operations leave no partial database state.
- Migration is restartable and marked complete only after commit.
- Reopen tests cover stores, transfers, schedules, settlements, combos, and
  audit records.
- The application database and journal are the only authoritative runtime
  persistence files.

