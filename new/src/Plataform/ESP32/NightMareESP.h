// Starts the NightMareESP system, initializing all necessary components and starting the main loop.
// This should be called AFTER we set custom handlers.
// This will: 
// - start wifi task
// - connect to mqtt
// - Schedule Telemetry jobs
void startNightMareESP();