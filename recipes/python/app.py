import socketserver
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"python recipe ok\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class Server(HTTPServer):
    """HTTPServer.server_bind() calls socket.getfqdn(), which blocks for several
    seconds (or forever) when the rootfs has no /etc/resolv.conf. Bind the plain
    way and fill in the name ourselves so the port is open immediately."""

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


Server(("0.0.0.0", 8000), Handler).serve_forever()
