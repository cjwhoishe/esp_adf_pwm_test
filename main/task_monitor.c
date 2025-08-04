//
// based on
// https://github.com/VPavlusha/ESP32_Task_Monitor
//
// and the esp_idf example in
// 
// .../esp-idf-v5.4.2/examples/system/freertos/real_time_stats
//
#include "task_monitor.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/param.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "driver/gpio.h"


#define OUTPUT_GPIO 8

static const char *TAG = "TASK_MONITOR";

#if !defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) || !defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    #error "configUSE_TRACE_FACILITY and configGENERATE_RUN_TIME_STATS must be defined as 1 in FreeRTOSConfig.h!"
#endif

#define ARRAY_SIZE_OFFSET 5                                 // allows for up to 5 more tasks to be created between finding the no of tasks and actually reading them
#define MILLISECONDS_PER_SECOND 1000

// Configure task monitor
#define TASK_MONITOR_PERIOD_IN_MS (10 * MILLISECONDS_PER_SECOND) // Run-time statistics will refresh every 10 seconds
#define TASK_MONITOR_CORE_AFFINITY tskNO_AFFINITY

#define COLOR_BLACK   "30"
#define COLOR_RED     "31"
#define COLOR_GREEN   "32"
#define COLOR_YELLOW  "33"
#define COLOR_BLUE    "34"
#define COLOR_PURPLE  "35"
#define COLOR_CYAN    "36"
#define COLOR_WHITE   "37"

#define COLOR(COLOR) "\033[0;" COLOR "m"
#define RESET_COLOR  "\033[0m"

#define BLACK   COLOR(COLOR_BLACK)
#define RED     COLOR(COLOR_RED)
#define GREEN   COLOR(COLOR_GREEN)
#define YELLOW  COLOR(COLOR_YELLOW)
#define BLUE    COLOR(COLOR_BLUE)
#define PURPLE  COLOR(COLOR_PURPLE)
#define CYAN    COLOR(COLOR_CYAN)
#define WHITE   COLOR(COLOR_WHITE)

typedef int (*CompareFunction)(const TaskStatus_t *, const TaskStatus_t *);

static void swap_tasks(TaskStatus_t *task1, TaskStatus_t *task2)
{
    if ((task1 == NULL) || (task2 == NULL)) return;
//    if ((task1 <= 1000) || (task2 <= 1000)) return;
    TaskStatus_t temp = *task1;
    *task1 = *task2;
    *task2 = temp;
}

static void generic_sort(TaskStatus_t *tasks_status_array, size_t number_of_tasks, CompareFunction compare)
{
    if (number_of_tasks > 1) {
        for (size_t i = 0; i <= number_of_tasks; ++i) {
            for (size_t k = i + 1; k < number_of_tasks; ++k) {
                if (compare(&tasks_status_array[i], &tasks_status_array[k])) {
                    swap_tasks(&tasks_status_array[i], &tasks_status_array[k]);
                }
            }
        }
    }
}

static int compare_by_runtime(const TaskStatus_t *task1, const TaskStatus_t *task2)
{
//    if ((task1 <= 1000) || (task2 <= 1000)){
    if ((task1 == NULL) || (task2 == NULL)){
        ESP_LOGI(TAG,"compare_by_core passed a null pointer task1: %ld task2: %ld", (long)task1, (long)task2);
        return 0;
    }   
        return task1->ulRunTimeCounter < task2->ulRunTimeCounter;

}

static int compare_by_core(const TaskStatus_t *task1, const TaskStatus_t *task2)
{
//    if ((task1 <= 1000) || (task2 <= 1000)){
    if ((task1 == NULL) || (task2 == NULL)){
        ESP_LOGI(TAG,"compare_by_core passed a null pointer task1: %ld task2: %ld", (long)task1, (long)task2);
        return 0;
    }   
    return task1->xCoreID > task2->xCoreID;
}

static void sort_tasks_by_runtime(TaskStatus_t *tasks_status_array, size_t number_of_tasks)
{
    generic_sort(tasks_status_array, number_of_tasks, compare_by_runtime);
}

static void sort_tasks_by_core(TaskStatus_t *tasks_status_array, size_t number_of_tasks)
{
    generic_sort(tasks_status_array, number_of_tasks, compare_by_core);

    size_t i;
    for (i = 0; tasks_status_array[i].xCoreID == 0; ++i);

    sort_tasks_by_runtime(tasks_status_array, i);
    if (i != number_of_tasks){
        // has 2 cores
        sort_tasks_by_runtime(&tasks_status_array[i], number_of_tasks - i);
    }
}

static uint32_t get_current_time_ms(void)
{
    return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ;
}

static float get_current_heap_free_percent(void)
{
    size_t current_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    return ((float) current_size / total_size) * 100.0;
}

static float get_minimum_heap_free_percent(void)
{
    size_t minimum_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    return ((float) minimum_size / total_size) * 100.0;
}

static const char* int_to_string(int number, char *string)
{
    sprintf(string, "%d", number);
    return string;
}

static const char* task_state_to_string(eTaskState state)
{
    switch (state) {
        case eRunning:
            return "Running";
        case eReady:
            return "Ready";
        case eBlocked:
            return "Blocked";
        case eSuspended:
            return "Suspended";
        case eDeleted:
            return "Deleted";
        case eInvalid:
            return "Invalid";
        default:
            return "Unknown state";
    }
}
bool led_on = true;

static void task_status_monitor_task(void *params)
{
    UBaseType_t number_of_tasks_start;
    UBaseType_t number_of_tasks_finish;
    UBaseType_t number_of_tasks_common;
    TaskStatus_t *start_tasks_status_array;
    TaskStatus_t *finished_tasks_status_array;
    TaskStatus_t *common_tasks_status_array;
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;
    uint32_t total_elapsed_time;

    esp_err_t _result = ESP_OK;
    while (true) {
        number_of_tasks_start = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
        start_tasks_status_array = (TaskStatus_t *)malloc(number_of_tasks_start * sizeof(TaskStatus_t));
        if (start_tasks_status_array == NULL){
        _result = ESP_ERR_INVALID_SIZE;
        goto normal_exit;
        }
        number_of_tasks_start = uxTaskGetSystemState(start_tasks_status_array, number_of_tasks_start, &start_run_time);
        if (number_of_tasks_start == 0) {
            _result = ESP_ERR_INVALID_SIZE;
            goto normal_exit;
        }
        
        vTaskDelay(pdMS_TO_TICKS(TASK_MONITOR_PERIOD_IN_MS));

        number_of_tasks_finish = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
        finished_tasks_status_array = (TaskStatus_t *)malloc(number_of_tasks_finish * sizeof(TaskStatus_t));
        number_of_tasks_finish = uxTaskGetSystemState(finished_tasks_status_array, number_of_tasks_finish, &end_run_time);
        if (number_of_tasks_finish == 0) {
            _result = ESP_ERR_INVALID_SIZE;
            goto normal_exit;
        }
        //Calculate total_elapsed_time in units of run time stats clock period.
        total_elapsed_time = (end_run_time - start_run_time);
        if (total_elapsed_time == 0) {
            _result = ESP_ERR_INVALID_STATE;
            goto normal_exit;
        }
            if (total_elapsed_time > 0) {  // Avoid divide by zero error
                
                printf("I (%lu) tm: " BLUE "Total Number Tasks at the start - %d and the end - %d over a time period of %lu ms\n"
                    RESET_COLOR,
                    get_current_time_ms(),
                    number_of_tasks_start,
                    number_of_tasks_finish,
                    total_elapsed_time/1000
                );


                printf("I (%lu) tm: " BLUE "%-18.16s %-11.10s %-6.5s %-8.8s %-10.9s %-11.10s %-15.15s %-s"
                    RESET_COLOR,
                    get_current_time_ms(),
                    "TASK NAME:",
                    "STATE:",
                    "CORE:",
                    "NUMBER:",
                    "PRIORITY:",
                    "STACK_MIN:",
                    "RUNTIME, µs:",
                    "RUNTIME, %:\n"
                );
               number_of_tasks_common = MAX(number_of_tasks_start,number_of_tasks_finish) + ARRAY_SIZE_OFFSET;
                common_tasks_status_array = (TaskStatus_t *)malloc(number_of_tasks_common * sizeof(TaskStatus_t));
                if (common_tasks_status_array == NULL){
                    _result = ESP_ERR_INVALID_SIZE;
                    goto normal_exit;
                }
                // scan for common ones and move to common_tasks for possible sorting later
    //Match each task in start_array to those in the end_array
                number_of_tasks_common = 0;
                for (int i = 0; i < number_of_tasks_start; i++) {
                    for (int j = 0; j < number_of_tasks_finish; j++) {
                    if ((start_tasks_status_array[i].xHandle == finished_tasks_status_array[j].xHandle) && (start_tasks_status_array[i].xHandle != NULL)){
                        // task same and not marked as being seen already as the same
                        //Mark that task have been matched by overwriting their handles and copy it into the common ones
                        common_tasks_status_array[number_of_tasks_common].xHandle = finished_tasks_status_array[j].xHandle;
                        common_tasks_status_array[number_of_tasks_common].xHandle = finished_tasks_status_array[j].xHandle;
                        common_tasks_status_array[number_of_tasks_common].pcTaskName = finished_tasks_status_array[j].pcTaskName;
                        common_tasks_status_array[number_of_tasks_common].xTaskNumber = finished_tasks_status_array[j].xTaskNumber;
                        common_tasks_status_array[number_of_tasks_common].eCurrentState = finished_tasks_status_array[j].eCurrentState;
                        common_tasks_status_array[number_of_tasks_common].uxCurrentPriority = finished_tasks_status_array[j].uxCurrentPriority;
                        common_tasks_status_array[number_of_tasks_common].uxBasePriority = finished_tasks_status_array[j].uxBasePriority;
                        common_tasks_status_array[number_of_tasks_common].ulRunTimeCounter = finished_tasks_status_array[j].ulRunTimeCounter - start_tasks_status_array[i].ulRunTimeCounter;
#if ( ( portSTACK_GROWTH > 0 ) && ( configRECORD_STACK_HIGH_ADDRESS == 1 ) )
                        common_tasks_status_array[number_of_tasks_common].pxTopOfStack = finished_tasks_status_array[j].pxTopOfStack;
                        common_tasks_status_array[number_of_tasks_common].pxEndOfStack = finished_tasks_status_array[j].pxEndOfStack;
#endif
                        common_tasks_status_array[number_of_tasks_common].usStackHighWaterMark = finished_tasks_status_array[j].usStackHighWaterMark;
#if ( configTASKLIST_INCLUDE_COREID == 1 )
                        common_tasks_status_array[number_of_tasks_common].xCoreID = finished_tasks_status_array[j].xCoreID;
#endif
                        number_of_tasks_common++;
                        start_tasks_status_array[i].xHandle = NULL;
                        finished_tasks_status_array[j].xHandle = NULL;
                        break;
                        }
                    }
               }
//               sort_tasks_by_core(common_tasks_status_array, number_of_tasks_common);
//              this causes panic restarts - TODO looking into it
                for (size_t i = 0; i < number_of_tasks_common; ++i) {

                    const char * string;  // 10 - maximum number of characters for int
                    const char * color;
                    if (xTaskGetCoreID(common_tasks_status_array[i].xHandle) == tskNO_AFFINITY){
                        string = "Any";
                        color = YELLOW;
                    } else {
                        if (xTaskGetCoreID(common_tasks_status_array[i].xHandle) == 0){
                        string = "Pro";
                        color = GREEN;
                        } else {
                        string = "App";
                        color = CYAN;
                        }
                    }; 
                    printf("I (%lu) tm: %s%-18.16s %-11.10s %-6.5s %-8d %-10d %-11lu %-14lu %-10.3f\n"
                        RESET_COLOR,
                        get_current_time_ms(),
                        color,
                        common_tasks_status_array[i].pcTaskName,
                        task_state_to_string(common_tasks_status_array[i].eCurrentState),
                        string,
                        common_tasks_status_array[i].xTaskNumber,
                        common_tasks_status_array[i].uxCurrentPriority,
                        common_tasks_status_array[i].usStackHighWaterMark,
                        common_tasks_status_array[i].ulRunTimeCounter,
                       (common_tasks_status_array[i].ulRunTimeCounter * 100.0) / total_elapsed_time);
                }

            //Print unmatched tasks
            for (int i = 0; i < number_of_tasks_start; i++) {
                if (start_tasks_status_array[i].xHandle != NULL) {
                    printf("I (%lu) tm: " YELLOW " %s | Deleted\n" RESET_COLOR ,get_current_time_ms(), start_tasks_status_array[i].pcTaskName);
                }
            }
            for (int i = 0; i < number_of_tasks_finish; i++) {
                if (finished_tasks_status_array[i].xHandle != NULL) {
                    printf("I (%lu) tm: " YELLOW " %s | Created\n" RESET_COLOR ,get_current_time_ms(), finished_tasks_status_array[i].pcTaskName);
                }
            }

                printf("I (%lu) tm: " YELLOW "Total heap free size:   " GREEN "%d[%d]" YELLOW " bytes\n" RESET_COLOR,
                    get_current_time_ms(), heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_total_size(MALLOC_CAP_INTERNAL));

                printf("I (%lu) tm: " YELLOW "Current heap free size: " GREEN "%d[%d]" YELLOW " bytes (" GREEN "%.2f"
                    YELLOW " %%)\n" RESET_COLOR, get_current_time_ms(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    get_current_heap_free_percent());

                printf("I (%lu) tm: " YELLOW "Minimum heap free size: " GREEN "%d" YELLOW " bytes (" GREEN "%.2f"
                    YELLOW " %%)\n" RESET_COLOR, get_current_time_ms(),
                    heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT), get_minimum_heap_free_percent());

                printf("I (%lu) tm: " YELLOW "Snapshot Time: " GREEN "%lu" YELLOW " µs (" GREEN "%lu"  YELLOW
                    " ms)\n" RESET_COLOR, get_current_time_ms(), total_elapsed_time, total_elapsed_time / 1000);

                uint64_t current_time = esp_timer_get_time();
                printf("I (%lu) tm: " YELLOW "System UpTime: " GREEN "%llu" YELLOW " µs (" GREEN "%llu" YELLOW
                    " seconds)\n\n" RESET_COLOR, get_current_time_ms(), current_time, current_time / 1000000);
            }
        free (finished_tasks_status_array);
        free (start_tasks_status_array);
        free (common_tasks_status_array);
        finished_tasks_status_array = NULL;
        start_tasks_status_array = NULL;
        common_tasks_status_array = NULL;
    }

normal_exit:
    free (finished_tasks_status_array);
    free (start_tasks_status_array);
    free (common_tasks_status_array);
}

esp_err_t task_monitor(void)
{
    BaseType_t status = xTaskCreatePinnedToCore(task_status_monitor_task, "monitor_task", configMINIMAL_STACK_SIZE * 2,
                                                NULL, tskIDLE_PRIORITY + 1, NULL, TASK_MONITOR_CORE_AFFINITY);
    if (status != pdPASS) {
        printf("I (%lu) tm: task_status_monitor_task(): Task was not created. Could not allocate required memory\n",
            get_current_time_ms());
        return ESP_ERR_NO_MEM;
    }
    printf("I (%lu) tm: task_status_monitor_task() started successfully\n", get_current_time_ms());
    return ESP_OK;
}
