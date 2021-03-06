/*
 * kernel_utils.c
 *
 * Created: 15.09.2019 17:03:39
 *  Author: ThePetrovich
 */

#include "tasks.h"
#include "scheduler.h"
#include "../kernel.h"
#include "../utils/linkedlists.h"
#include "../platform/platform.h"
#include "../memory/memory.h"
#include "../memory/heap.h"
#include <kernel/tasks.h>
#include <kernel/kernel_config.h>
#include <kernel/ktypes.h>
#include <kernel/kdefs.h>
#include <kdebug/debug.h>
#include <kernel/memory.h>

static volatile uint16_t kGlobalPid = 0;

static volatile struct kLinkedListStruct_t kReadyTaskList[CFG_NUMBER_OF_PRIORITIES];
static volatile struct kLinkedListStruct_t kSleepingTaskList;
static volatile struct kLinkedListStruct_t kSuspendedTaskList;

static const size_t kTaskStructSize	= (sizeof(struct kTaskStruct_t) + ((size_t)(CFG_PLATFORM_BYTE_ALIGNMENT - 1))) & ~((size_t)CFG_PLATFORM_BYTE_ALIGNMENT_MASK);

static volatile kTaskHandle_t kIdleTaskHandle;

volatile struct kLinkedListStruct_t* tasks_getReadyTaskListArray()
{
	return kReadyTaskList;
}

volatile struct kLinkedListStruct_t* tasks_getReadyTaskListPtr(uint8_t priority)
{
	return &kReadyTaskList[priority];
}

volatile struct kLinkedListStruct_t* tasks_getSleepingTaskListPtr()
{
	return &kSleepingTaskList;
}

kTaskHandle_t tasks_getIdleTaskHandle()
{
	return kIdleTaskHandle;
}

kReturnValue_t tasks_init(kTask_t idle)
{
	kIdleTaskHandle = tasks_createTask(idle, NULL, 100, KPRIO_IDLE, KTASK_SYSTEM, "idle");

	if (kIdleTaskHandle == NULL) {
		debug_logMessage(PGM_PUTS, L_FATAL, PSTR("\r\ntaskmgr: Startup failed, could not create idle task.\r\n"));
		while(1);
	}

	tasks_initScheduler(kIdleTaskHandle);
	tasks_setCurrentTask(kIdleTaskHandle);
	tasks_setNextTask(kIdleTaskHandle);

	return 0;
}

void tasks_setTaskState(kTaskHandle_t task, kTaskState_t state)
{
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)task);

	if (sanityCheck == 0) {
		switch (state) {
			case KSTATE_UNINIT:
				utils_listDeleteAny(task->activeTaskListItem.list, &(task->activeTaskListItem));
				task->state = KSTATE_UNINIT;
			break;
			case KSTATE_SUSPENDED:
				utils_listDeleteAny(task->activeTaskListItem.list, &(task->activeTaskListItem));
				utils_listAddBack(&kSuspendedTaskList, &(task->activeTaskListItem));
				task->state = KSTATE_SUSPENDED;
			break;
			case KSTATE_SLEEPING:
				utils_listDeleteAny(task->activeTaskListItem.list, &(task->activeTaskListItem));
				utils_listAddBack(&kSleepingTaskList, &(task->activeTaskListItem));
				task->state = KSTATE_SLEEPING;
			break;
			case KSTATE_BLOCKED:
				task->state = KSTATE_BLOCKED;
			break;
			case KSTATE_READY:
				utils_listDeleteAny(task->activeTaskListItem.list, &(task->activeTaskListItem));
				utils_listAddBack(&kReadyTaskList[task->priority], &(task->activeTaskListItem));
				task->state = KSTATE_READY;
			break;
			case KSTATE_RUNNING:
				task->state = KSTATE_RUNNING;
			break;
			default:
				#if CFG_LOGGING == 1
				debug_logMessage(PGM_PUTS, L_ERROR, PSTR("taskmgr: Invalid parameter in setTaskState.\r\n"));
				#endif
			break;
		}
	}

	threads_endAtomicOperation(sreg);
}

kReturnValue_t tasks_setTaskPriority(kTaskHandle_t task, uint8_t priority)
{
	kReturnValue_t exitcode = ERR_GENERIC;
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)task);

	if (sanityCheck == 0) {
		if (priority <= CFG_NUMBER_OF_PRIORITIES) {
			task->priority = priority;
			if (task->state == KSTATE_READY) {
				tasks_setTaskState(task, KSTATE_READY);
			}
		}
		else {
			exitcode = CFG_NUMBER_OF_PRIORITIES;
		}
	}

	threads_endAtomicOperation(sreg);
	return exitcode;
}

static inline void tasks_setupTaskStructure(kTaskHandle_t task, \
												kTask_t startupPointer, \
												kStackPtr_t stackPointer, \
												kStackPtr_t stackBegin, \
												kStackSize_t stackSize, \
												void* args, \
												uint8_t priority, \
												kTaskState_t state, \
												kTaskType_t type, \
												char* name)
{
	task -> stackPtr = stackPointer;
	task -> stackBegin = stackBegin;
	task -> stackSize = stackSize;
	task -> taskPtr = startupPointer;
	task -> taskArgs = args;
	task -> priority = priority;
	task -> activeLock = NULL;
	task -> state = state;
	task -> type = type;
	task -> pid = kGlobalPid;
	task -> name = name;
}

kReturnValue_t tasks_createTaskStatic(kStackPtr_t memory, kTaskHandle_t* handle, kTask_t entry, void* args, kStackSize_t stackSize, uint8_t priority, kTaskType_t type, char* name)
{
	kReturnValue_t exitcode = ERR_GENERIC;
	kStatusRegister_t sreg = threads_startAtomicOperation();

	if (entry != NULL) {
		if (memory != NULL) {
			((kTaskHandle_t)memory)->activeTaskListItem.data = memory;
			
			kStackPtr_t stackPrepared = NULL;
			
			#if CFG_MEMORY_PROTECTION_MODE == 2 || CFG_MEMORY_PROTECTION_MODE == 3
				#if CFG_STACK_GROWTH_DIRECTION == 0
					stackPrepared = platform_prepareStackFrame(memory + kTaskStructSize + CFG_STACK_SAFETY_MARGIN, stackSize, entry, args);
					memory_prepareProtectionRegion((void*)(memory + kTaskStructSize), CFG_STACK_SAFETY_MARGIN);
				#else
					stackPrepared = platform_prepareStackFrame(memory + kTaskStructSize, stackSize, entry, args);
					memory_prepareProtectionRegion((void*)(memory + kTaskStructSize + stackSize), CFG_STACK_SAFETY_MARGIN);
				#endif
			#else
				stackPrepared = platform_prepareStackFrame(memory + kTaskStructSize, stackSize, entry, args);
			#endif
			
			//TODO: assert
			if (stackPrepared == NULL) {
				kernel_panic(PSTR("Runtime assert: tasks_createTaskStatic: stackPrepared = NULL\r\n"));
			}
			
			tasks_setupTaskStructure((kTaskHandle_t)memory, entry, stackPrepared, memory + kTaskStructSize, stackSize, args, priority, KSTATE_READY, type, name);

			tasks_setTaskState((kTaskHandle_t)memory, KSTATE_READY);

			kGlobalPid++;
			exitcode = 0;
			if (handle != NULL) {
				*handle = (kTaskHandle_t)memory;
			}
		}
		else {
			exitcode = -3;
		}
	}

	threads_endAtomicOperation(sreg);
	return exitcode;
}

kReturnValue_t tasks_createTaskDynamic(kTaskHandle_t* handle, kTask_t entry, void* args, kStackSize_t stackSize, uint8_t priority, kTaskType_t type, char* name)
{
	kReturnValue_t exitcode = ERR_GENERIC;
	kStatusRegister_t sreg = threads_startAtomicOperation();

	//if (stackSize < CFG_MIN_STACK_SIZE) {
	//	stackSize = CFG_MIN_STACK_SIZE;
	//}

	#if CFG_MEMORY_PROTECTION_MODE == 2 || CFG_MEMORY_PROTECTION_MODE == 3
		kStackPtr_t stackPointer = (kStackPtr_t)memory_heapAlloc(stackSize + kTaskStructSize + CFG_STACK_SAFETY_MARGIN);
	#else
		kStackPtr_t stackPointer = (kStackPtr_t)memory_heapAlloc(stackSize + kTaskStructSize);
	#endif

	exitcode = tasks_createTaskStatic(stackPointer, NULL, entry, args, stackSize, priority, type, name);

	if (exitcode != 0) {
		memory_heapFree((void*)stackPointer);
	}
	else {
		*handle = (kTaskHandle_t)(stackPointer);
	}

	threads_endAtomicOperation(sreg);
	return exitcode;
}

kTaskHandle_t tasks_createTask(kTask_t entry, void* args, kStackSize_t stackSize, uint8_t priority, kTaskType_t type, char* name)
{
	kTaskHandle_t returnValue = NULL;
	
	#if CFG_LOGGING == 1
		kReturnValue_t result = tasks_createTaskDynamic(&returnValue, entry, args, stackSize, priority, type, name);
		switch (result) {
			case 0:
				debug_logMessage(PGM_PUTS, L_INFO, PSTR("taskmgr: Successfully created a new task\r\n"));
			break;
			case -1:
				debug_logMessage(PGM_PUTS, L_INFO, PSTR("taskmgr: Task creation error[-1]: entryPoint is NULL\r\n"));
			break;
			case -2:
				debug_logMessage(PGM_PUTS, L_INFO, PSTR("taskmgr: Task creation error[-2]: failed to allocate task heap\r\n"));
			break;
			case -3:
				debug_logMessage(PGM_PUTS, L_INFO, PSTR("taskmgr: Task creation error[-3]: failed to allocate task structure\r\n"));
			break;
			default:
				debug_logMessage(PGM_PUTS, L_INFO, PSTR("taskmgr: Task creation error[]: unknown error\r\n"));
			break;
		}
	#else
		tasks_createTaskDynamic(&returnValue, entry, args, stackSize, priority, type, name);
	#endif
	
	return returnValue;
}

kReturnValue_t tasks_removeTask(kTaskHandle_t task)
{
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)task);

	if (sanityCheck == 0) {
		tasks_setTaskState(task, KSTATE_UNINIT);
		memory_heapFree((void*)task);
	}

	threads_endAtomicOperation(sreg);
	return 0;
}

//TODO: Fix these
/*
void taskmgr_restartTask(kTaskHandle_t task)
{
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)task);

	if (sanityCheck == 0) {
		kStackPtr_t stackPrepared = platform_prepareStackFrame(task->stackBegin, task->stackSize, task->taskPtr, task->args);

		task->stackPtr = stackPrepared;
		task->lock = NULL;
		task->taskList.next = NULL;
		task->taskList.prev = NULL;
		task->taskList.list = NULL;

		#if CFG_MEMORY_PROTECTION_MODE == 2 || CFG_MEMORY_PROTECTION_MODE == 3
		memory_prepareProtectionRegion((void*)(task->stackBegin + task->stackSize), CFG_STACK_SAFETY_MARGIN);
		#endif

		taskmgr_setTaskState(task, KSTATE_READY);
	}

	threads_endAtomicOperation(sreg);
	return;
}

kTaskHandle_t taskmgr_forkTask(kTaskHandle_t task)
{
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)task);
	kTaskHandle_t handle = NULL;

	if (sanityCheck == 0) {
		handle = taskmgr_createTask(task->taskPtr, task->args, task->stackSize, task->priority, task->type, task->name);
	}

	threads_endAtomicOperation(sreg);
	return handle;
}

kReturnValue_t taskmgr_replaceTask(kTaskHandle_t taskToReplace, kTask_t entry, void* args)
{
	kStatusRegister_t sreg = threads_startAtomicOperation();

	kReturnValue_t sanityCheck = memory_pointerSanityCheck((void*)taskToReplace);

	if (sanityCheck == 0) {
		taskToReplace->taskPtr = entry;
		taskToReplace->args = args;
		taskmgr_restartTask(taskToReplace);
	}

	threads_endAtomicOperation(sreg);
	return 0;
}
*/