var http = require('http');
var fs = require('fs');
var index = fs.readFileSync('index.html');

const port = 8081;

console.log(`running node server on port: ${port}`);

http.createServer(function (req, res) {
  res.writeHead(200, {'Content-Type': 'text/html'});
  res.end(index);
  console.log('Request Received!');
}).listen(port);