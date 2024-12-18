/* 
 * Modified from the original code to integrate a CBS server and a button interrupt.
 * We slightly reduce Task3's compute time from 100ms to 80ms to free some CPU margin.
 *
 * Total utilization approx. now:
 * T1: 1000ms/3000ms ≈ 33.3%
 * T2:  250ms/ 750ms ≈ 33.3%
 * T3:   80ms/ 500ms = 16%
 * T4:   50ms/1000ms = 5%
 * T5:   50ms/1000ms = 5%
 * Sum ≈ 92.6%
 *
 * CBS server: Qs=30ms, Ts=1000ms = 3% utilization approx.
 * Total ≈ 95.6%
 *
 * Button press adds a 100ms job to CBS server. The server will not exceed its budget per period.
 */

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"
#include "queue.h"
#include "pico/stdlib.h"

/* Include the headers where you declared xTaskCreateEDF, xTaskCreateCBS, vCBSAddAperiodicJob */
#include "task.h"

/* Define the LED pin */
#define mainTASK_LED            ( PICO_DEFAULT_LED_PIN )

/* Define GPIO pins for each Task */
#define TASK1_GPIO_PIN 2
#define TASK2_GPIO_PIN 3
#define TASK3_GPIO_PIN 4
#define TASK4_GPIO_PIN 5
#define TASK5_GPIO_PIN 6
#define CBS_GPIO_PIN 8

/* Choose a button pin for interrupt */
#define BUTTON_GPIO_PIN 7

/* Periods in ms converted to ticks */
#define Period1 pdMS_TO_TICKS(3000)
#define Period2 pdMS_TO_TICKS(750)
#define Period3 pdMS_TO_TICKS(500)
#define Period4 pdMS_TO_TICKS(1000)
#define Period5 pdMS_TO_TICKS(1000)

/* Deadlines same as periods for simplicity */
#define Deadline1 Period1
#define Deadline2 Period2
#define Deadline3 Period3
#define Deadline4 Period4
#define Deadline5 Period5

/* Compute times in ms converted to ticks
   Adjusted Task3 compute time from 100ms to 80ms to leave room for CBS */
#define ComputeTime1 pdMS_TO_TICKS(800)
#define ComputeTime2 pdMS_TO_TICKS(220)
#define ComputeTime3 pdMS_TO_TICKS(80) 
#define ComputeTime4 pdMS_TO_TICKS(50)
#define ComputeTime5 pdMS_TO_TICKS(50)

/* Server parameters */
#define ServerPeriod pdMS_TO_TICKS(1000)
#define ServerMaxBudget pdMS_TO_TICKS(30) // 30ms budget

#define computeMultiplier 9050000/configTICK_RATE_HZ*1.25

/* Task function prototypes */
static void vTask1( void * pvParameters );
static void vTask2( void * pvParameters );
static void vTask3( void * pvParameters );
static void vTask4( void * pvParameters );
static void vTask5( void * pvParameters );
static void vCBSServerTask(void *pvParameters);
static void button_isr_callback(uint gpio, uint32_t events);

/* A global handle to the CBS server task so we can add jobs to it */
static TaskHandle_t xCBSServerHandle = NULL;
static QueueHandle_t xCBSServerQueue = NULL;


volatile TickType_t firstTaskStartTime = 0;
volatile BaseType_t firstTaskStartTimeSet = pdFALSE;

/**
 * @brief The main function that sets up the hardware, creates tasks, and starts the scheduler.
 */
void main_blinky( void )
{
    // Initialize stdio for debugging (optional)
    stdio_init_all();

    // Initialize the LED pin
    gpio_init( mainTASK_LED );
    gpio_set_dir( mainTASK_LED, GPIO_OUT );
    gpio_put( mainTASK_LED, 1 );

    // Initialize GPIO pins for all tasks
    gpio_init( TASK1_GPIO_PIN );
    gpio_set_dir( TASK1_GPIO_PIN, GPIO_OUT );

    gpio_init( TASK2_GPIO_PIN );
    gpio_set_dir( TASK2_GPIO_PIN, GPIO_OUT );

    gpio_init( TASK3_GPIO_PIN );
    gpio_set_dir( TASK3_GPIO_PIN, GPIO_OUT );

    gpio_init( TASK4_GPIO_PIN );
    gpio_set_dir( TASK4_GPIO_PIN, GPIO_OUT );

    gpio_init( TASK5_GPIO_PIN );
    gpio_set_dir( TASK5_GPIO_PIN, GPIO_OUT );

    gpio_init( CBS_GPIO_PIN );
    gpio_set_dir( CBS_GPIO_PIN, GPIO_OUT );

    // Initialize button input with pull-up
    gpio_init(BUTTON_GPIO_PIN);
    gpio_set_dir(BUTTON_GPIO_PIN, GPIO_OUT);


    // Create tasks with their deadlines (using EDF)
    xTaskCreateEDF( vTask1, "Task1", configMINIMAL_STACK_SIZE, NULL, NULL, Deadline1 );
    xTaskCreateEDF( vTask2, "Task2", configMINIMAL_STACK_SIZE, NULL, NULL, Deadline2 );
    xTaskCreateEDF( vTask3, "Task3", configMINIMAL_STACK_SIZE, NULL, NULL, Deadline3 );
    xTaskCreateEDF( vTask4, "Task4", configMINIMAL_STACK_SIZE, NULL, NULL, Deadline4 );
    xTaskCreateEDF( vTask5, "Task5", configMINIMAL_STACK_SIZE, NULL, NULL, Deadline5 );

    xCBSServerQueue = xQueueCreate(10, sizeof(TickType_t));
    // Create a CBS server task
    xTaskCreateCBS(vCBSServerTask, "CBSServer", configMINIMAL_STACK_SIZE, (void*)xCBSServerQueue,
                   ServerPeriod, ServerMaxBudget, &xCBSServerHandle);

    // Set up an interrupt on the falling edge (button pressed)
    gpio_set_irq_enabled_with_callback(BUTTON_GPIO_PIN, GPIO_IRQ_EDGE_FALL, true, &button_isr_callback);

    // Start the scheduler
    vTaskStartScheduler();

    // Infinite loop (should never reach here)
    for( ;; );
}

/**
 * @brief Task1 simulates a heavy load (1000 ms compute every 3000 ms).
 */
static void vTask1( void * pvParameters )
{
    // Set the first task start time if it hasn't been set yet
    if ( !firstTaskStartTimeSet )
    {
        firstTaskStartTime = xTaskGetTickCount();
        firstTaskStartTimeSet = pdTRUE;
    }

    // Sync the task to the first task start time so that periods are consistent
    TickType_t xLastWakeTime = firstTaskStartTime;
    vTaskSetAbsoluteDeadline(NULL, firstTaskStartTime + Period1);

    volatile int i = 0;

    for( ;; )
    {
        gpio_put( TASK1_GPIO_PIN, 1 );
        i = 0;
        while( i < ComputeTime1 * computeMultiplier )
        {
            gpio_put( TASK1_GPIO_PIN, 1 );
            i++;
        }
        gpio_put( TASK1_GPIO_PIN, 0 );
        xTaskDelayUntil( &xLastWakeTime, Period1 );
    }
}

/**
 * @brief Task2 simulates a load of 250 ms every 750 ms.
 */
static void vTask2( void * pvParameters )
{
    // Set the first task start time if it hasn't been set yet
    if ( !firstTaskStartTimeSet )
    {
        firstTaskStartTime = xTaskGetTickCount();
        firstTaskStartTimeSet = pdTRUE;
    }

    // Sync the task to the first task start time so that periods are consistent
    TickType_t xLastWakeTime = firstTaskStartTime;
    vTaskSetAbsoluteDeadline(NULL, firstTaskStartTime + Period2);

    volatile int i = 0;

    for( ;; )
    {
        gpio_put( TASK2_GPIO_PIN, 1 );
        i = 0;
        while( i < ComputeTime2 * computeMultiplier )
        {
            gpio_put( TASK2_GPIO_PIN, 1 );
            i++;
        }
        gpio_put( TASK2_GPIO_PIN, 0 );
        xTaskDelayUntil( &xLastWakeTime, Period2 );
    }
}

/**
 * @brief Task3 simulates a load of 80 ms every 500 ms (16% utilization).
 */
static void vTask3( void * pvParameters )
{
    // Set the first task start time if it hasn't been set yet
    if ( !firstTaskStartTimeSet)
    {
        firstTaskStartTime = xTaskGetTickCount();
        firstTaskStartTimeSet = pdTRUE;
    }

    // Sync the task to the first task start time so that periods are consistent
    TickType_t xLastWakeTime = firstTaskStartTime;
    vTaskSetAbsoluteDeadline(NULL, firstTaskStartTime + Period3);

    volatile int i = 0;

    for( ;; )
    {
        gpio_put( TASK3_GPIO_PIN, 1 );
        i = 0;
        while( i < ComputeTime3 * computeMultiplier )
        {
            gpio_put( TASK3_GPIO_PIN, 1 );
            i++;
        }
        gpio_put( TASK3_GPIO_PIN, 0 );

        xTaskDelayUntil( &xLastWakeTime, Period3 );
    }
}

/**
 * @brief Task4 simulates a small load of 50 ms every 1000 ms (~5%).
 */
static void vTask4( void * pvParameters )
{
    // Set the first task start time if it hasn't been set yet
    if (!firstTaskStartTimeSet)
    {
        firstTaskStartTime = xTaskGetTickCount();
        firstTaskStartTimeSet = pdTRUE;
    }

    // Sync the task to the first task start time so that periods are consistent
    TickType_t xLastWakeTime = firstTaskStartTime;
    vTaskSetAbsoluteDeadline(NULL, firstTaskStartTime + Period4);

    volatile int i = 0;

    for( ;; )
    {
        gpio_put( TASK4_GPIO_PIN, 1 );
        i = 0;
        while( i < ComputeTime4 * computeMultiplier )
        {
            gpio_put( TASK4_GPIO_PIN, 1 );
            i++;
        }
        gpio_put( TASK4_GPIO_PIN, 0 );

        xTaskDelayUntil( &xLastWakeTime, Period4 );
    }
}

/**
 * @brief Task5 simulates a small load of 50 ms every 1000 ms (~5%).
 */
static void vTask5( void * pvParameters )
{
    // Set the first task start time if it hasn't been set yet
    if (!firstTaskStartTimeSet)
    {
        firstTaskStartTime = xTaskGetTickCount();
        firstTaskStartTimeSet = pdTRUE;
    }

    // Sync the task to the first task start time so that periods are consistent
    TickType_t xLastWakeTime = firstTaskStartTime;
    vTaskSetAbsoluteDeadline(NULL, firstTaskStartTime + Period5);

    volatile int i = 0;

    for( ;; )
    {
        gpio_put( TASK5_GPIO_PIN, 1 );
        i = 0;
        while( i < ComputeTime5 * computeMultiplier )
        {
            gpio_put( TASK5_GPIO_PIN, 1 );
            i++;
        }
        gpio_put( TASK5_GPIO_PIN, 0 );

        xTaskDelayUntil( &xLastWakeTime, Period5 );
    }
}

static void button_isr_callback(uint gpio, uint32_t events)
{
    TickType_t xComputeTimeForJob = pdMS_TO_TICKS(100); // A 100ms job, for example.

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Since this is an ISR, use xQueueSendFromISR for thread safety
    xQueueSendFromISR(xCBSServerQueue, &xComputeTimeForJob, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief The CBS server task that waits for aperiodic jobs and runs them.
 */
static void vCBSServerTask(void *pvParameters)
{
    QueueHandle_t xCBSServerQueue = (QueueHandle_t) pvParameters;
    TickType_t xJobComputeTime;
    volatile int i = 0;
   

    for (;;)
    {

        i++;
        gpio_put(CBS_GPIO_PIN, 1); // Using LED pin to indicate server busy
        
        // /* Block waiting for a job from the CBS queue */
        // if (xQueueReceive(xCBSServerQueue, &xJobComputeTime, portMAX_DELAY) == pdTRUE)
        // {
        //     /* Run a busy loop for xJobComputeTime ms (similar to other tasks) */
        //     i = 0;
        //     gpio_put(CBS_GPIO_PIN, 1); // Using LED pin to indicate server busy
        //     while (i < xJobComputeTime * computeMultiplier)
        //     {
        //         gpio_put(CBS_GPIO_PIN,1);
        //         i++;
        //     }
        //     gpio_put(CBS_GPIO_PIN, 0);
        //     /* After finishing the job, the loop continues and waits for next job */
        // }
    }
}
