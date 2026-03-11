use std::path::PathBuf;

fn main() {
    embuild::espidf::sysenv::output();

    // Copy partitions.csv to the build output directory so esp-idf-sys
    // can find it when CONFIG_PARTITION_TABLE_CUSTOM_FILENAME is set.
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let src = PathBuf::from("partitions.csv");
    if src.exists() {
        std::fs::copy(&src, out_dir.join("partitions.csv"))
            .expect("Failed to copy partitions.csv to OUT_DIR");
    }
}
