use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::prelude::Peripherals;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::sys::link_patches;
use log::info;

mod bridge;
mod storage;
mod timer;
mod voice;

fn main() -> anyhow::Result<()> {
    link_patches();
    EspLogger::initialize_default();

    info!("Radio starting up...");

    let peripherals = Peripherals::take()?;
    let sys_loop = EspSystemEventLoop::take()?;

    // Initialize storage (NVS)
    storage::init()?;

    // Initialize timer
    timer::init()?;

    // Initialize C++ UI bridge — spawns LVGL task
    unsafe {
        bridge::ui::ui_bridge_init();
    }

    // Start the C++ UI task in a dedicated FreeRTOS task
    std::thread::Builder::new()
        .name("ui_task".into())
        .stack_size(8192)
        .spawn(|| unsafe {
            bridge::ui::ui_bridge_task_run();
        })?;

    // Subscribe to input events from C++ UI
    bridge::events::subscribe_all(&sys_loop)?;

    info!("Radio initialized successfully");

    // Main loop — keep the main task alive
    loop {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}
