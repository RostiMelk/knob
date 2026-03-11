use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::prelude::Peripherals;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
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
    let nvs_partition = EspDefaultNvsPartition::take()?;

    // Initialize storage (NVS)
    let settings = storage::init(nvs_partition)?;
    info!("Loaded settings: vol={}, station={}", settings.volume, settings.station_index);

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
    bridge::events::EventSubscriber::new()
        .on_encoder_turn(|delta| {
            info!("Encoder: delta={}", delta.delta);
            // TODO: Route to volume or station change
        })
        .on_wifi(|connected| {
            info!("WiFi: {}", if connected { "connected" } else { "disconnected" });
            bridge::ui::set_wifi_status(connected);
        })
        .on_voice_button(|| {
            info!("Voice button pressed");
            // TODO: Toggle voice mode
        })
        .subscribe()?;

    info!("Radio initialized successfully");

    // Main loop — keep the main task alive
    loop {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}
