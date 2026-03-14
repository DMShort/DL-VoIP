/// Minimal healthcheck binary for Docker HEALTHCHECK.
/// Sends a raw HTTP GET to /health and checks for a 200 response.
/// No external dependencies (no curl needed in the container).
use std::io::{Read, Write};
use std::net::TcpStream;
use std::time::Duration;

fn main() {
    let host = std::env::var("HEALTH_HOST").unwrap_or_else(|_| "127.0.0.1".to_string());
    let port = std::env::var("HEALTH_PORT").unwrap_or_else(|_| "9000".to_string());
    let addr = format!("{}:{}", host, port);

    let result = check_health(&addr);
    std::process::exit(if result { 0 } else { 1 });
}

fn check_health(addr: &str) -> bool {
    let mut stream = match TcpStream::connect_timeout(
        &addr.parse().unwrap_or_else(|_| {
            std::net::SocketAddr::from(([127, 0, 0, 1], 9000))
        }),
        Duration::from_secs(5),
    ) {
        Ok(s) => s,
        Err(_) => return false,
    };

    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let _ = stream.set_write_timeout(Some(Duration::from_secs(5)));

    let request = format!(
        "GET /health HTTP/1.0\r\nHost: {}\r\nConnection: close\r\n\r\n",
        addr
    );

    if stream.write_all(request.as_bytes()).is_err() {
        return false;
    }

    let mut response = String::new();
    if stream.read_to_string(&mut response).is_err() {
        return false;
    }

    response.starts_with("HTTP/1.") && response.contains(" 200 ")
}
