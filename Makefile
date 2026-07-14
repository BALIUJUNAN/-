# C terminal application with AbyssDB storage foundation.

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic
CPPFLAGS = -I.
LDFLAGS =

-include config.mk
ABYSS_ROOT ?= ../AbyssDB
ABYSS_CPPFLAGS = -I$(ABYSS_ROOT)/include -I$(ABYSS_ROOT)/src
ABYSS_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic

ifeq ($(OS),Windows_NT)
    TARGET = supermarket.exe
    TEST_PHASE2_TARGET = .test-build/test_storage_phase2.exe
    TEST_PHASE3_TARGET = .test-build/test_repository_phase3.exe
    TEST_PHASE4_TARGET = .test-build/test_base_entities_phase4.exe
    TEST_PHASE5_TARGET = .test-build/test_migration_phase5.exe
    TEST_PHASE6_TARGET = .test-build/test_sales_phase6.exe
    TEST_PHASE7_TARGET = .test-build/test_inventory_phase7.exe
    TEST_PHASE8_TARGET = .test-build/test_purchase_phase8.exe
    TEST_PHASE9_TARGET = .test-build/test_supplier_finance_phase9.exe
    TEST_PHASE10_TARGET = .test-build/test_vip_phase10.exe
    TEST_PHASE11_TARGET = .test-build/test_store_transfer_phase11.exe
    TEST_PHASE12_TARGET = .test-build/test_control_phase12.exe
    PLATFORM_LIBS = -lbcrypt -lm
else
    TARGET = supermarket
    TEST_PHASE2_TARGET = .test-build/test_storage_phase2
    TEST_PHASE3_TARGET = .test-build/test_repository_phase3
    TEST_PHASE4_TARGET = .test-build/test_base_entities_phase4
    TEST_PHASE5_TARGET = .test-build/test_migration_phase5
    TEST_PHASE6_TARGET = .test-build/test_sales_phase6
    TEST_PHASE7_TARGET = .test-build/test_inventory_phase7
    TEST_PHASE8_TARGET = .test-build/test_purchase_phase8
    TEST_PHASE9_TARGET = .test-build/test_supplier_finance_phase9
    TEST_PHASE10_TARGET = .test-build/test_vip_phase10
    TEST_PHASE11_TARGET = .test-build/test_store_transfer_phase11
    TEST_PHASE12_TARGET = .test-build/test_control_phase12
    PLATFORM_LIBS = -lm
    CFLAGS += -pthread
    ABYSS_CFLAGS += -pthread
    LDFLAGS += -pthread
endif

APP_SRCS = main.c supermarket.c sale.c purchase.c schedule.c report.c \
           marketing.c finance.c store_ops.c utility.c ui.c \
           app/sm_finance_legacy.c app/sm_vip_legacy.c \
           app/sm_store_legacy.c app/sm_control_legacy.c
STORAGE_SRCS = storage/sm_store.c storage/sm_key.c storage/sm_codec.c
REPO_SRCS = repo/sm_repository.c repo/sm_base_codec.c \
            repo/sm_base_repository.c repo/sm_sales_codec.c \
            repo/sm_sales_repository.c repo/sm_inventory_codec.c \
            repo/sm_inventory_repository.c repo/sm_purchase_codec.c \
            repo/sm_purchase_repository.c repo/sm_finance_codec.c \
           repo/sm_finance_repository.c
REPO_SRCS += repo/sm_operations_codec.c repo/sm_store_repository.c \
             repo/sm_control_repository.c
CONTEXT_SRCS = app/sm_app_context.c app/sm_base_service.c \
               app/sm_sales_service.c app/sm_inventory_service.c \
               app/sm_purchase_service.c app/sm_finance_service.c \
               migration/sm_legacy_import.c migration/sm_sales_import.c \
               migration/sm_inventory_import.c migration/sm_purchase_import.c
CONTEXT_SRCS += migration/sm_finance_import.c
CONTEXT_SRCS += app/sm_store_service.c app/sm_control_service.c \
                migration/sm_operations_import.c
SRCS = $(APP_SRCS) $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS)
OBJS = $(SRCS:.c=.o)

ABYSS_SOURCE_NAMES = abyss_core.c abyss_options.c abyss_error.c \
    abyss_allocator.c abyss_sync.c abyss_vfs.c abyss_vfs_posix.c \
    abyss_vfs_win32.c abyss_manifest.c abyss_page.c abyss_verify.c \
    abyss_btree.c abyss_overflow.c abyss_freelist.c abyss_wal.c \
    abyss_checkpoint.c abyss_recovery.c abyss_snapshot.c abyss_entity.c
ABYSS_OBJS = $(addprefix .abyss-build/,$(ABYSS_SOURCE_NAMES:.c=.o))

.DELETE_ON_ERROR:
.PHONY: all clean test test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7 test-phase8 test-phase9 test-phase10 test-phase11 test-phase12

all: $(TARGET)

$(TARGET): $(OBJS) $(ABYSS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(PLATFORM_LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -c $< -o $@

.abyss-build:
	mkdir .abyss-build

.abyss-build/%.o: $(ABYSS_ROOT)/src/%.c | .abyss-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(ABYSS_CFLAGS) -c $< -o $@

.test-build:
	mkdir .test-build

$(TEST_PHASE2_TARGET): tests/test_storage_phase2.c $(STORAGE_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_storage_phase2.c $(STORAGE_SRCS) $(ABYSS_OBJS) \
		$(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE3_TARGET): tests/test_repository_phase3.c $(STORAGE_SRCS) $(REPO_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_repository_phase3.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE4_TARGET): tests/test_base_entities_phase4.c $(STORAGE_SRCS) $(REPO_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_base_entities_phase4.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE5_TARGET): tests/test_migration_phase5.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_migration_phase5.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE6_TARGET): tests/test_sales_phase6.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_sales_phase6.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE7_TARGET): tests/test_inventory_phase7.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_inventory_phase7.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE8_TARGET): tests/test_purchase_phase8.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_purchase_phase8.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE9_TARGET): tests/test_supplier_finance_phase9.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_supplier_finance_phase9.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE10_TARGET): tests/test_vip_phase10.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_vip_phase10.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE11_TARGET): tests/test_store_transfer_phase11.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_store_transfer_phase11.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

$(TEST_PHASE12_TARGET): tests/test_control_phase12.c $(STORAGE_SRCS) $(REPO_SRCS) $(CONTEXT_SRCS) $(ABYSS_OBJS) | .test-build
	$(CC) $(CPPFLAGS) $(ABYSS_CPPFLAGS) $(CFLAGS) -o $@ \
		tests/test_control_phase12.c $(STORAGE_SRCS) $(REPO_SRCS) \
		$(CONTEXT_SRCS) $(ABYSS_OBJS) $(LDFLAGS) $(PLATFORM_LIBS)

test: test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7 test-phase8 test-phase9 test-phase10 test-phase11 test-phase12

test-phase2: $(TEST_PHASE2_TARGET)
	$(TEST_PHASE2_TARGET)

test-phase3: $(TEST_PHASE3_TARGET)
	$(TEST_PHASE3_TARGET)

test-phase4: $(TEST_PHASE4_TARGET)
	$(TEST_PHASE4_TARGET)

test-phase5: $(TEST_PHASE5_TARGET)
	$(TEST_PHASE5_TARGET)

test-phase6: $(TEST_PHASE6_TARGET)
	$(TEST_PHASE6_TARGET)

test-phase7: $(TEST_PHASE7_TARGET)
	$(TEST_PHASE7_TARGET)

test-phase8: $(TEST_PHASE8_TARGET)
	$(TEST_PHASE8_TARGET)

test-phase9: $(TEST_PHASE9_TARGET)
	$(TEST_PHASE9_TARGET)

test-phase10: $(TEST_PHASE10_TARGET)
	$(TEST_PHASE10_TARGET)

test-phase11: $(TEST_PHASE11_TARGET)
	$(TEST_PHASE11_TARGET)

test-phase12: $(TEST_PHASE12_TARGET)
	$(TEST_PHASE12_TARGET)

ifeq ($(OS),Windows_NT)
clean:
	-@cmd /C "del /Q *.o app\*.o migration\*.o storage\*.o repo\*.o .abyss-build\*.o .test-build\*.exe supermarket.exe 2>nul"
else
clean:
	-$(RM) $(OBJS) $(TARGET) $(ABYSS_OBJS) $(TEST_PHASE2_TARGET) $(TEST_PHASE3_TARGET) $(TEST_PHASE4_TARGET) $(TEST_PHASE5_TARGET) $(TEST_PHASE6_TARGET) $(TEST_PHASE7_TARGET) $(TEST_PHASE8_TARGET) $(TEST_PHASE9_TARGET) $(TEST_PHASE10_TARGET) $(TEST_PHASE11_TARGET) $(TEST_PHASE12_TARGET)
endif
