use std::path::PathBuf;

fn main() {
    embuild::espidf::sysenv::output();

    // Copy partitions.csv to the esp-idf-sys build output directory.
    // When CONFIG_PARTITION_TABLE_CUSTOM is set, the ESP-IDF build
    // expects partitions.csv in the project output directory, which
    // for esp-idf-sys is OUT_DIR (the build script's output).
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let src = PathBuf::from("partitions.csv");
    if src.exists() {
        // Copy to OUT_DIR itself (where build.rs outputs go)
        let _ = std::fs::copy(&src, out_dir.join("partitions.csv"));
        // Also copy to the parent 'out' directory where esp-idf-sys
        // sets up the CMake project
        if let Some(parent) = out_dir.parent() {
            let _ = std::fs::copy(&src, parent.join("partitions.csv"));
        }
    }
}
