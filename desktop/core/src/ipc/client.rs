//! The console side of the IPC link.

use anyhow::Context;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::tcp::{OwnedReadHalf, OwnedWriteHalf};
use tokio::net::TcpStream;

use crate::ipc::discovery_file_path;
use crate::ipc::protocol::{IpcRequest, IpcResponse, RequestKind};

pub struct IpcClient {
    reader: BufReader<OwnedReadHalf>,
    writer: OwnedWriteHalf,
    auth_token: String,
}

impl IpcClient {
    /// Reads the daemon's discovery file and connects. Fails with a clear
    /// message if the daemon isn't running (no discovery file, or nothing
    /// listening on the port it last published) rather than hanging.
    pub async fn connect() -> anyhow::Result<Self> {
        let path = discovery_file_path()?;
        let content = std::fs::read_to_string(&path)
            .with_context(|| format!("daemon discovery file not found at {} - is quarzlampe-tray running?", path.display()))?;
        let mut lines = content.lines();
        let port: u16 = lines
            .next()
            .context("discovery file missing port")?
            .parse()
            .context("discovery file has a malformed port")?;
        let auth_token = lines.next().context("discovery file missing auth token")?.to_string();

        let stream = TcpStream::connect(("127.0.0.1", port))
            .await
            .with_context(|| format!("could not connect to daemon on port {port} - is it still running?"))?;
        let (reader, writer) = stream.into_split();
        Ok(Self { reader: BufReader::new(reader), writer, auth_token })
    }

    pub async fn request(&mut self, kind: RequestKind) -> anyhow::Result<IpcResponse> {
        let req = IpcRequest { auth_token: self.auth_token.clone(), kind };
        let mut payload = serde_json::to_string(&req)?;
        payload.push('\n');
        self.writer.write_all(payload.as_bytes()).await?;

        let mut line = String::new();
        let n = self.reader.read_line(&mut line).await?;
        if n == 0 {
            anyhow::bail!("daemon closed the connection");
        }
        Ok(serde_json::from_str(&line)?)
    }
}
