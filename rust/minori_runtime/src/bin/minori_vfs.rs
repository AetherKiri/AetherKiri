use std::env;
use std::path::PathBuf;

use aether_minori_runtime::vfs::{PazArchive, Vfs};

fn main() -> Result<(), String> {
    let mut args = env::args_os().skip(1);
    let path = args
        .next()
        .map(PathBuf::from)
        .ok_or("usage: minori-vfs GAME_ROOT|ARCHIVE [RESOURCE]")?;
    if path.is_dir() {
        let vfs = Vfs::mount_game(&path)?;
        if let Some(name) = args.next() {
            let name = name.to_string_lossy();
            let data = vfs.read(&name)?;
            if let Some(output) = args.next() {
                std::fs::write(&output, &data)
                    .map_err(|e| format!("write {}: {e}", PathBuf::from(output).display()))?;
            }
            println!(
                "resource={name} size={} prefix={}",
                data.len(),
                prefix(&data)
            );
        } else {
            for (archive, entry) in vfs.entries() {
                println!(
                    "{archive}\t{}\t{}\t{}",
                    entry.unpacked_size, entry.size, entry.name
                );
            }
        }
    } else {
        let archive = PazArchive::open(&path)?;
        if let Some(name) = args.next() {
            let name = name.to_string_lossy();
            let data = archive.read(&name)?;
            if let Some(output) = args.next() {
                std::fs::write(&output, &data)
                    .map_err(|e| format!("write {}: {e}", PathBuf::from(output).display()))?;
            }
            println!(
                "resource={name} size={} prefix={}",
                data.len(),
                prefix(&data)
            );
        } else {
            let mut entries: Vec<_> = archive.entries().collect();
            entries.sort_by_key(|entry| entry.offset);
            for entry in entries {
                println!("{}\t{}\t{}", entry.unpacked_size, entry.size, entry.name);
            }
        }
    }
    Ok(())
}

fn prefix(data: &[u8]) -> String {
    data.iter()
        .take(16)
        .map(|value| format!("{value:02X}"))
        .collect::<Vec<_>>()
        .join("")
}
