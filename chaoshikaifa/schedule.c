/**
 * @file schedule.c
 * @brief 排班管理模块 - 员工排班、周排班表
 */

#include "supermarket.h"
#include <stdlib.h>

// ==================== 排班数据 ====================
Schedule *g_schedules = NULL;

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
    *count = 0;
    
    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->employee_id == employee_id) {
            result = (Schedule**)realloc(result, (*count + 1) * sizeof(Schedule*));
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
    *count = 0;
    
    Schedule *sch = g_schedules;
    while (sch) {
        if (sch->year == year && sch->week == week) {
            result = (Schedule**)realloc(result, (*count + 1) * sizeof(Schedule*));
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
 * 打印排班表
 */
void print_schedule_table(int year, int week) {
    printf("\n========== %d年第%d周排班表 ==========\n", year, week);
    printf("%-10s %-10s %-6s %-6s %-6s %-6s %-6s %-6s\n", 
           "员工ID", "姓名", "周一", "周二", "周三", "周四", "周五", "周六/日");
    printf("------------------------------------------------------------\n");
    
    int count = 0;
    Schedule **scheds = list_week_schedules(year, week, &count);
    
    for (int i = 0; i < count; i++) {
        Schedule *sch = scheds[i];
        Employee *emp = find_employee_by_id(sch->employee_id);
        
        printf("%-10d %-10s %-6s %-6s %-6s %-6s %-6s %-6s\n",
            sch->employee_id,
            emp ? emp->name : "未知",
            get_shift_name(sch->shifts[0]),
            get_shift_name(sch->shifts[1]),
            get_shift_name(sch->shifts[2]),
            get_shift_name(sch->shifts[3]),
            get_shift_name(sch->shifts[4]),
            get_shift_name(sch->shifts[5]));
    }
    
    free(scheds);
    printf("================================\n\n");
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
    
    snprintf(content, sizeof(content), "%d|%d|%d|%d|%s|%s|%s|%s|%s|%s|%s|%ld",
        sch->id, sch->employee_id, sch->year, sch->week,
        sch->shifts[0], sch->shifts[1], sch->shifts[2],
        sch->shifts[3], sch->shifts[4], sch->shifts[5],
        sch->shifts[6], (long)sch->created_at);
    
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
        sch->id = atoi(strtok(line, "|"));
        sch->employee_id = atoi(strtok(NULL, "|"));
        sch->year = atoi(strtok(NULL, "|"));
        sch->week = atoi(strtok(NULL, "|"));
        
        for (int i = 0; i < 7; i++) {
            strncpy(sch->shifts[i], strtok(NULL, "|"), 3);
        }
        sch->created_at = (time_t)atoi(strtok(NULL, "|"));
        
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
