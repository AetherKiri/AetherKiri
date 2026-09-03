use std::collections::HashMap;

use crate::command::CommandKind;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Statement {
    Empty,
    Comment,
    Control {
        name: String,
        arguments: Vec<String>,
    },
    Label(String),
    Include(String),
    Command {
        kind: CommandKind,
        arguments: Vec<String>,
    },
    Message(String),
}

#[derive(Debug, Default, Clone)]
pub struct Program {
    pub statements: Vec<Statement>,
    pub labels: HashMap<String, usize>,
}

impl Program {
    pub fn parse(source: &str) -> Result<Self, String> {
        let mut program = Program::default();
        for (line_number, line) in source.lines().enumerate() {
            let statement =
                parse_line(line).map_err(|e| format!("line {}: {e}", line_number + 1))?;
            if let Statement::Label(label) = &statement
                && program
                    .labels
                    .insert(label.clone(), program.statements.len())
                    .is_some()
            {
                return Err(format!("line {}: duplicate label {label}", line_number + 1));
            }
            program.statements.push(statement);
        }
        Ok(program)
    }
}

pub fn parse_line(line: &str) -> Result<Statement, String> {
    let line = line.trim();
    if line.is_empty() {
        return Ok(Statement::Empty);
    }
    if let Some(rest) = line.strip_prefix('@') {
        let mut fields = split_arguments(rest)?;
        if fields.is_empty() {
            return Err("at-sign control name is empty".into());
        }
        let name = fields.remove(0);
        return Ok(Statement::Control {
            name,
            arguments: fields,
        });
    }
    if let Some(rest) = line.strip_prefix("#include") {
        let path = rest.trim().trim_matches('"');
        if path.is_empty() {
            return Err("include path is empty".into());
        }
        return Ok(Statement::Include(path.to_string()));
    }
    if let Some(rest) = line.strip_prefix('.') {
        if let Some(message) = rest.strip_prefix("message ") {
            return Ok(Statement::Command {
                kind: CommandKind::Message,
                arguments: split_message_arguments(message)?,
            });
        }
        let mut fields = split_arguments(rest)?;
        if fields.is_empty() {
            return Err("command name is empty".into());
        }
        let name = fields.remove(0);
        if name == "label" {
            let label = fields.first().ok_or("label name is empty")?;
            return Ok(Statement::Label(label.clone()));
        }
        let kind = CommandKind::from_name(&name)
            .ok_or_else(|| format!("command is not in the Minori registry: .{name}"))?;
        return Ok(Statement::Command {
            kind,
            arguments: fields,
        });
    }
    Ok(Statement::Message(line.to_string()))
}

fn split_message_arguments(source: &str) -> Result<Vec<String>, String> {
    let (id, rest) = source
        .split_once(' ')
        .ok_or("message requires id, voice, speaker, and text")?;
    let (voice, rest) = rest
        .split_once(' ')
        .ok_or("message requires voice, speaker, and text")?;
    let (speaker, text) = rest
        .split_once(' ')
        .ok_or("message requires speaker and text")?;
    Ok(vec![id.into(), voice.into(), speaker.into(), text.into()])
}

fn split_arguments(source: &str) -> Result<Vec<String>, String> {
    let mut fields = Vec::new();
    let mut field = String::new();
    let mut quoted = false;
    let mut escaped = false;
    for ch in source.chars() {
        if escaped {
            field.push(ch);
            escaped = false;
            continue;
        }
        if ch == '\\' && quoted {
            escaped = true;
            continue;
        }
        if ch == '"' {
            quoted = !quoted;
            continue;
        }
        if ch.is_whitespace() && !quoted {
            if !field.is_empty() {
                fields.push(std::mem::take(&mut field));
            }
        } else {
            field.push(ch);
        }
    }
    if quoted {
        return Err("unterminated quoted argument".into());
    }
    if !field.is_empty() {
        fields.push(field);
    }
    Ok(fields)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_minori_control_lines() {
        assert_eq!(
            parse_line(".label start").unwrap(),
            Statement::Label("start".into())
        );
        assert_eq!(
            parse_line(".goto start").unwrap(),
            Statement::Command {
                kind: CommandKind::Goto,
                arguments: vec!["start".into()],
            }
        );
        assert_eq!(
            parse_line("@load ad image.png").unwrap(),
            Statement::Control {
                name: "load".into(),
                arguments: vec!["ad".into(), "image.png".into()],
            }
        );
        assert_eq!(
            parse_line(".message 100   \\a\\vtext").unwrap(),
            Statement::Command {
                kind: CommandKind::Message,
                arguments: vec!["100".into(), "".into(), "".into(), "\\a\\vtext".into()],
            }
        );
        assert_eq!(
            parse_line(".message 130 voice speaker words with spaces").unwrap(),
            Statement::Command {
                kind: CommandKind::Message,
                arguments: vec![
                    "130".into(),
                    "voice".into(),
                    "speaker".into(),
                    "words with spaces".into(),
                ],
            }
        );
    }
}
