/**
 * @file schedule.c
 * @brief 排班管理模块 - 员工周排班、排班表显示、班次统计
 *
 * 排班数据结构：
 *   每条排班记录 = 一个员工 + 一年/一周 + 7天班次
 *   shifts[0]~shifts[6] 对应周一~周日，值为 "早"/"晚"/"休"/""
 *
 * 业务规则：
 *   - 同一员工同一周只能有一条排班记录（batch_create_schedule 做唯一性检查）
 *   - 班次类型：早班("早")、晚班("晚")、休息("休")
 *
 * 数据存储：schedule.txt（每行一条排班记录）
 */

#include "supermarket.h"
#include <stdlib.h>

// ==================== 排班数据 ====================
static Schedule *g_schedules = NULL;  // 排班记录链表

// ==================== 排班操作 ====================

/**
 * 创建排班记录
 */
int create_schedule(Schedule *schedule) {
    schedule->id = generate_id();
    schedule->created_at = time(NULL);
    
    Schedule *new_sch = (Schedule*)malloc(sizeof(Schedule));
    *new_sch = *schedule;
    new_sch->next = g_schedules;
    g_schedules = new_sch;
    
    // 保存到文件
    save_schedule(new_sch);
    
    return schedule->id;
}

/**
 * 更新排班
 */
int update_schedule(int schedule_id, char shifts[7][4]) {
    Schedule *sch = find_schedule(schedule_id);
    if (!sch) return -1;
    
    for (int i = 0; i < 7; i++) {
        strncpy(sch->shifts[i], shifts[i], 3);
    }
    
    save_schedule(sch);
    return 0;
}

/**
 * 查找排班记录
 */
Schedule* find_schedule(int schedule_id) {
    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->id == schedule_id) return sch;
        sch = sch->next;
    }
    return NULL;
}

/**
 * 按员工和周查找排班
 */
Schedule* find_schedule_by_employee_week(int employee_id, int year, int week) {
    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->employee_id == employee_id && 
            sch->year == year && sch->week == week) {
            return sch;
        }
        sch = sch->next;
    }
    return NULL;
}

/**
 * 批量创建周排班
 */
int batch_create_schedule(int employee_id, int year, int week, char shifts[7][4]) {
    // 检查是否已存在
    Schedule *existing = find_schedule_by_employee_week(employee_id, year, week);
    if (existing) {
        printf("该员工 %d 第 %d 年第 %d 周排班已存在\n", employee_id, year, week);
        return existing->id;
    }
    
    Schedule schedule;
    memset(&schedule, 0, sizeof(Schedule));
    schedule.employee_id = employee_id;
    schedule.year = year;
    schedule.week = week;
    
    for (int i = 0; i < 7; i++) {
        strncpy(schedule.shifts[i], shifts[i], 3);
    }
    
    return create_schedule(&schedule);
}

/**
 * 列出某员工所有排班
 */
Schedule** list_employee_schedules(int employee_id, int *count) {
    Schedule **result = NULL;
    int capacity = 0;
    *count = 0;

    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->employee_id == employee_id) {
            if (*count >= capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                result = (Schedule**)realloc(result, capacity * sizeof(Schedule*));
            }
            result[*count] = sch;
            (*count)++;
        }
        sch = sch->next;
    }

    return result;
}

/**
 * 列出某周所有排班
 */
Schedule** list_week_schedules(int year, int week, int *count) {
    Schedule **result = NULL;
    int capacity = 0;
    *count = 0;

    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->year == year && sch->week == week) {
            if (*count >= capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                result = (Schedule**)realloc(result, capacity * sizeof(Schedule*));
            }
            result[*count] = sch;
            (*count)++;
        }
        sch = sch->next;
    }

    return result;
}

/**
 * 获取班次名称
 */
const char* get_shift_name(const char *shift) {
    if (strcmp(shift, SHIFT_MORNING) == 0) return "早班";
    if (strcmp(shift, SHIFT_EVENING) == 0) return "晚班";
    if (strcmp(shift, SHIFT_OFF) == 0) return "休息";
    return "";
}

/**
 * 计算字符串的显示宽度（中文字符占2列，ASCII字符占1列）
 */
static int display_width(const char *str) {
    int width = 0;
    while (*str) {
        if ((unsigned char)*str > 0x7F) {
            width += 2;  // 中文字符占2列
            str += 2;    // UTF-8中文通常3字节，但Windows控制台可能是GBK(2字节)
            // 安全跳过：跳过所有高位字节
            while (*str && (unsigned char)*str > 0x7F) str++;
        } else {
            width += 1;
            str++;
        }
    }
    return width;
}

/**
 * 将字符串填充到指定显示宽度（右补空格）
 */
static void pad_to_width(char *out, int out_size, const char *str, int target_width) {
    int w = display_width(str);
    int pad = target_width - w;
    if (pad < 0) pad = 0;

    int written = snprintf(out, out_size, "%s", str);
    if (written < 0) written = 0;
    for (int i = 0; i < pad && written < out_size - 1; i++) {
        out[written++] = ' ';
    }
    out[written] = '\0';
}

/**
 * 打印排班表
 * 显示指定年份和周数的排班表，包含周一到周日共7天
 */
void print_schedule_table(int year, int week) {
    // 使用定宽列对齐（中文字符用pad_to_width处理）
    const int COL_ID = 10;
    const int COL_NAME = 10;
    const int COL_DAY = 6;  // 每个班次列宽6

    printf("\n========== %d年第%d周排班表 ==========\n", year, week);

    // 表头
    char hdr[8][32];
    pad_to_width(hdr[0], 32, "员工ID", COL_ID);
    pad_to_width(hdr[1], 32, "姓名", COL_NAME);
    pad_to_width(hdr[2], 32, "周一", COL_DAY);
    pad_to_width(hdr[3], 32, "周二", COL_DAY);
    pad_to_width(hdr[4], 32, "周三", COL_DAY);
    pad_to_width(hdr[5], 32, "周四", COL_DAY);
    pad_to_width(hdr[6], 32, "周五", COL_DAY);
    pad_to_width(hdr[7], 32, "周六", COL_DAY);
    printf("%s%s%s%s%s%s%s%s",
           hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7]);

    // 周日单独打印（需要换行或额外列）
    char hdr_sun[32];
    pad_to_width(hdr_sun, 32, "周日", COL_DAY);
    printf("%s\n", hdr_sun);

    printf("-------------------------------------------------------------\n");

    int count = 0;
    Schedule **scheds = list_week_schedules(year, week, &count);

    for (int i = 0; i < count; i++) {
        Schedule *sch = scheds[i];
        Employee *emp = find_employee_by_id(sch->employee_id);

        char id_buf[32], name_buf[32], day_buf[7][32];
        snprintf(id_buf, sizeof(id_buf), "%d", sch->employee_id);
        pad_to_width(name_buf, 32, emp ? emp->name : "未知", COL_NAME);
        for (int d = 0; d < 7; d++) {
            pad_to_width(day_buf[d], 32, get_shift_name(sch->shifts[d]), COL_DAY);
        }

        // ID右对齐到COL_ID宽度
        int id_w = display_width(id_buf);
        int id_pad = COL_ID - id_w;
        if (id_pad < 0) id_pad = 0;
        printf("%*s%s%s%s%s%s%s%s%s",
               id_pad + id_w, id_buf, name_buf,
               day_buf[0], day_buf[1], day_buf[2], day_buf[3],
               day_buf[4], day_buf[5], day_buf[6]);
        printf("\n");
    }

    free(scheds);
    printf("=================================\n\n");
}

/**
 * 统计某班次人数
 */
void count_shifts_by_type(int year, int week) {
    int morning = 0, evening = 0, off = 0;
    
    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->year == year && sch->week == week) {
            for (int i = 0; i < 7; i++) {
                if (strcmp(sch->shifts[i], SHIFT_MORNING) == 0) morning++;
                else if (strcmp(sch->shifts[i], SHIFT_EVENING) == 0) evening++;
                else if (strcmp(sch->shifts[i], SHIFT_OFF) == 0) off++;
            }
        }
        sch = sch->next;
    }
    
    printf("\n========== %d年第%d周班次统计 ==========\n", year, week);
    printf("早班: %d 人次\n", morning);
    printf("晚班: %d 人次\n", evening);
    printf("休息: %d 人次\n", off);
    printf("================================\n\n");
}

// ==================== 文件操作 ====================

/**
 * 保存排班记录 - 原子追加写入
 */
int save_schedule(Schedule *sch) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/schedule.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%d|%d|%d|%s|%s|%s|%s|%s|%s|%s|%lld",
        sch->id, sch->employee_id, sch->year, sch->week,
        sch->shifts[0], sch->shifts[1], sch->shifts[2],
        sch->shifts[3], sch->shifts[4], sch->shifts[5],
        sch->shifts[6], (long long)sch->created_at);
    
    return atomic_append(filepath, content);
}

/**
 * 加载排班记录
 */
int load_schedules(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/schedule.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Schedule *sch = (Schedule*)malloc(sizeof(Schedule));
        memset(sch, 0, sizeof(Schedule));
        char *token;
        char *saveptr;
        token = strtok_r(line, "|", &saveptr); sch->id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sch->employee_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sch->year = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sch->week = token ? atoi(token) : 0;

        for (int i = 0; i < 7; i++) {
            token = strtok_r(NULL, "|", &saveptr);
            if (token) strncpy(sch->shifts[i], token, 3);
        }
        token = strtok_r(NULL, "|", &saveptr); sch->created_at = token ? (time_t)atoll(token) : 0;
        
        sch->next = g_schedules;
        g_schedules = sch;
        
        if (sch->id > g_auto_id_counter) {
            g_auto_id_counter = sch->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 删除排班
 */
int delete_schedule(int schedule_id) {
    Schedule **prev = &g_schedules;
    while (*prev) {
        if ((*prev)->id == schedule_id) {
            Schedule *to_free = *prev;
            *prev = (*prev)->next;
            free(to_free);
            return 0;
        }
        prev = &(*prev)->next;
    }
    return -1;
}
