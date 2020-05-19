/*
 * init.c
 *
 * Created: 13.02.2020 22:38:32
 *  Author: Admin
 */
#include <initd/initd.h>

void taskmgr_init(kTaskHandle_t* taskQueue, uint8_t taskIndex);

void user_preinit();
void user_init();
void user_postinit();

kTask kernel_idle1(void* args)
{
	while(1) {
		platform_NOP();
		debug_logMessage(PGM_PUTS, L_INFO, PSTR("idle: idle task debug output\r\n"));
	}
}

void kernel_preinit()
{
	hal_UART_INIT(12);
	debug_init();
	#if CFG_LOGGING == 1
		//debug_puts(L_INFO, PSTR("\x0C"));
		debug_puts(L_INFO, PSTR("kernel: Initializing debug uart interface, baud=38400\r\n"));
		debug_puts(L_INFO, PSTR("kernel: Firing up RTOS\r\n"));
		memmgr_heapInit();
	#endif
}

uint8_t kernel_startScheduler()
{
	#if CFG_LOGGING == 1
		debug_puts(L_INFO, PSTR("kernel: Starting up task manager"));
	#endif
	//debug_puts(L_INFO, PSTR(" kernel: Starting up task manager                      [OK]\r\n"));
	//.................................................................
	taskmgr_init(taskmgr_getTaskListPtr(), taskmgr_getTaskListIndex());

	#if CFG_LOGGING == 1
		debug_puts(L_NONE, PSTR("                      [OK]\r\n"));
		debug_puts(L_INFO, PSTR("kernel: Preparing safety memory barrier"));
	#endif

	//kernel_prepareMemoryBarrier(kernel_getStackPtr() + (CFG_TASK_STACK_SIZE + CFG_KERNEL_STACK_SAFETY_MARGIN)-1, CFG_KERNEL_STACK_SAFETY_MARGIN, 0xFE);

	#if CFG_LOGGING == 1
		debug_puts(L_NONE, PSTR("               [OK]\r\n"));
	#endif

	kTaskHandle_t ct = taskmgr_createTask(kernel_idle1, NULL, 64, KPRIO_IDLE, KTASK_SYSTEM, "idle");
	if (ct == NULL) {
		debug_puts(L_ERROR, PSTR("kernel: Failed to create idle task"));
		while(1);
	}
	ct -> pid = 0;

	#if CFG_LOGGING == 1
		debug_puts(L_INFO, PSTR("kernel: Starting up first task"));
	#endif

	taskmgr_setCurrentTask(ct);

	#if CFG_LOGGING == 1
		debug_puts(L_NONE, PSTR("                        [OK]\r\n"));
		debug_puts(L_INFO, PSTR("kernel: Setting up system timer"));
	#endif

	platform_setupSystemTimer();
	#if CFG_LOGGING == 1
		debug_puts(L_NONE, PSTR("                       [OK]\r\n"));
		debug_puts(L_INFO, PSTR("kernel: Starting up system timer"));
	#endif

	platform_startSystemTimer();

	#if CFG_LOGGING == 1
		debug_puts(L_NONE, PSTR("                      [OK]\r\n"));
		debug_puts(L_INFO, PSTR("kernel: System startup complete\r\n"));
	#endif

	platform_DELAY_MS(1000);

	debug_puts(L_INFO, PSTR("\x0C"));

	platform_ENABLE_INTERRUPTS();
	threads_exitCriticalSection();

	return 0;
}

void initd_startup()
{
	kernel_preinit();
	debug_puts(L_INFO, PSTR("initd: Pre-init phase\r\n"));
	user_preinit();

	debug_puts(L_INFO, PSTR("initd: Init phase\r\n"));
	user_init();

	debug_puts(L_INFO, PSTR("initd: Post-init phase\r\n"));
	user_postinit();

	debug_puts(L_INFO, PSTR("initd: Init complete, starting scheduler\r\n"));
	kernel_setSystemStatus(KOSSTATUS_RUNNING);
	kernel_startScheduler();
}
