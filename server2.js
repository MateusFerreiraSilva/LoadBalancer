var http = require('http');
var fs = require('fs');
var index = fs.readFileSync('index.html');

const port = 8082;

console.log(`running node server on port: ${port}`);

let totalRequests = 0;

http.createServer(function (req, res) {
  res.writeHead(200, {'Content-Type': 'text/html'});
  res.end(index);
  console.log(`Request Received!\nTotal Number of Requests: ${++totalRequests}`);
}).listen(port);