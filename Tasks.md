1- Documentation
	A- Sensor interface table:
	Add a table to README: sensor name, interface type, sampling period, normal range, invalid range, failure behavior. All info already exists in the .h files.
	B- RTOS task architecture table (expanded)
	Extend the existing README table with: stack size, WCET budget, core, queue(s) used. Values are all in the headers already.
	C- Requirement list (REQ-01 to REQ-12)
	Write the 12 requirements from the analysis into a structured table with ID, description, type, and test approach.
	D- Traceability matrix
	Map each REQ-ID to its INT/SYS/UNIT test ID. One table, two columns. Can be done once tests are named.



2- Firmware: UART test hooks
	A- Add TIMING command to uart\_task.cpp
	Print per-task WCET, period, and jitter counters already tracked by micros(). Just format and send via MCAL\_UART\_Respond().
	B- Add STACK command to uart\_task.cpp
	Call uxTaskGetStackHighWaterMark() for each task handle and print via UART. Handles already returned by \*\_Start() functions.
	C- Add FAULT\_ADC command
	Force battery ADC raw = 0 and raw = 4095 via a flag read in doSample(). Tests REQ-03 (invalid sensor range detection).
	D- Add STRESS\_ON / STRESS\_OFF command
	Spawn a temporary CPU-burn task on Core 0 that does busy-wait math. Needed for stress test and jitter measurement.



3- Unit tests
	A- Unit test: voltToPct() in battery\_mcal.cpp
	Host-side C++ test. Inputs: 4.15V→100%, 3.40V→0%, 3.75V→\~50%, clamp below 2.5V, clamp above 4.5V. No mock needed.
	B- Unit test: rmsToDB() and dbToPercent() in mic\_mcal.cpp
	Inputs: sumSq=0→30dB floor, full-scale→90dB ceil, midpoint. Pure math, no ADC stub needed.
	C- Unit test: UART command parser (processCommand)
	Test: missing args, unknown command, empty string, buffer-length boundary (127 chars), REBOOT path. Stub MCAL\_UART\_Respond().
	D- Unit test: WiFi state machine transitions
	Stub HAL\_WiFiRadio\_Status(). Test: CONNECTING→CONNECTED, timeout→retry, max retries→FAILED→SETUP\_PORTAL.



4- Firmware: timing instrumentation
	A- Add micros() WCET tracking to Mic\_Task
	At loop top: t0=esp\_timer\_get\_time(). After MCAL\_Mic\_Tick(): delta=now-t0. Track max delta. Expose via TIMING command.
	B- Add WCET tracking to Heart\_Task
	Same pattern. Track max MCAL\_Heart\_Tick() execution time and worst period jitter over rolling 60-s window.
	C- Add WCET tracking to UI\_Task and WiFi\_Task
	Track vTaskDelayUntil overruns (missed deadlines). Increment a counter when actual period > target period.
	D- Enable FreeRTOS runtime stats
	Set configGENERATE\_RUN\_TIME\_STATS=1 and configUSE\_STATS\_FORMATTING\_FUNCTIONS=1 in FreeRTOSConfig.h. Add RTOS\_STATS UART command to print vTaskGetRunTimeStats().



5- Firmware: RTOS primitives
	A- Add UART mutex (xSemaphoreCreateMutex)
	Create g\_uartMutex in main. Wrap every HAL\_UART\_Printf / HAL\_UART\_SendLine call with Take/Give. Prevents garbled output from concurrent tasks.
	B- Add I2C bus mutex
	Create g\_i2cMutex. Wrap all Wire transactions in heart\_mcal.cpp and oled\_mcal.cpp. Both tasks share the same I2C bus — this is a real race.
	C- Add binary semaphore for button event signaling
	Create g\_btnSemaphore. UI\_Task calls xSemaphoreTake(g\_btnSemaphore, pdMS\_TO\_TICKS(20)) instead of a bare vTaskDelayUntil. Gives semaphore from poll or ISR.
	D- Attach GPIO ISR to SELECT button (GPIO10)
	gpio\_isr\_handler\_add(BTN\_SELECT\_PIN, btn\_isr, NULL). ISR calls xSemaphoreGiveFromISR(g\_btnSemaphore). Satisfies the ISR requirement.



6- Demonstrations

&#x09;A- Race condition demo: unsafe shared counter
	Add RACE\_UNSAFE command: two tasks increment volatile int sharedCounter 10000x each without protection. Print final value (will be < 20000).
	B- Race condition demo: safe version with mutex
	Add RACE\_SAFE command: same test but with portENTER\_CRITICAL/EXIT or a mutex. Final value must equal 20000.
	C- Priority inversion demo
	Add PRIO\_INV command: Low-priority task holds I2C mutex with injected 500ms delay. High-priority Heart\_Task blocks. Mid-priority CPU burn runs. Measure Heart deadline miss.



7- Test execution
	A- Run integration tests INT-01 through INT-08
	Each sensor alone → with task → with queue → with processing → full system → under load. Document one integration bug found.
	B- Run system tests SYS-01 through SYS-09
	Normal op, boundary values, sensor disconnect, WiFi failure, queue overflow, 10-min run, reset/recovery.
	C- Run fault injection tests (all 8 faults)
	ADC out of range, digital stuck, I2C disconnect, queue overflow, delayed consumer, mutex held long, CPU load, semaphore burst.
	D- Run 10-minute stress test
	Enable STRESS\_ON. Record max queue occupancy, dropped samples, worst WCET, worst jitter, missed deadlines, watchdog events.
	E- Collect all timing measurements (Phase 5)
	WCET all tasks, period jitter, queue latency, semaphore wakeup, mutex blocking time, CPU load %, stack HWM.



8- Test automation: (BONUS)
	A-test\_runner.py: serial port connection + command sender
	Opens COM port at 115200, sends UART commands (STATUS, HEART, FAULT\_ADC, STRESS\_ON), reads responses with timeout.
	B-test\_runner.py: log parser and pass/fail checker
	Regex patterns for WCET, jitter, BPM, queue state. Compare against REQ thresholds. Print PASS/FAIL per requirement.
	C-test\_runner.py: CSV report generator
	Write results to test\_results.csv with timestamp, test ID, measured value, threshold, and pass/fail status.

