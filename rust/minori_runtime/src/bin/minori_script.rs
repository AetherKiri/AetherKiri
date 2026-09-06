use std::collections::BTreeMap;
use std::env;
use std::path::PathBuf;

use aether_minori_runtime::script::{Program, Statement};
use encoding_rs::SHIFT_JIS;

fn main() -> Result<(), String> {
    let paths: Vec<PathBuf> = env::args_os().skip(1).map(PathBuf::from).collect();
    if paths.is_empty() {
        return Err("usage: minori-script SCRIPT.sc [...]".into());
    }

    let mut commands = BTreeMap::<String, usize>::new();
    let mut statement_count = 0usize;
    for path in &paths {
        let data = std::fs::read(path).map_err(|error| format!("{}: {error}", path.display()))?;
        let (source, _, had_errors) = SHIFT_JIS.decode(&data);
        if had_errors {
            return Err(format!("{}: invalid Shift-JIS", path.display()));
        }
        let program =
            Program::parse(&source).map_err(|error| format!("{}: {error}", path.display()))?;
        statement_count += program.statements.len();
        for statement in program.statements {
            if let Statement::Command { kind, .. } = statement {
                *commands.entry(kind.name().to_string()).or_default() += 1;
            }
        }
    }

    println!(
        "scripts={} statements={} commands={}",
        paths.len(),
        statement_count,
        commands.values().sum::<usize>()
    );
    for (name, count) in commands {
        println!(".{name}\t{count}");
    }
    Ok(())
}
