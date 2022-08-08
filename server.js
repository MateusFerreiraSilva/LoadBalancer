const http = require('http');
const fs = require('fs');
const index = fs.readFileSync('index.html');
const readline = require('readline');

function runServer(port) {
  console.log(`running node server on port: ${port}`);
  let totalRequests = 0;

  http.createServer(function (req, res) {
    res.writeHead(200, {'Content-Type': 'text/html'});
    res.end(index);
    console.log(`Request Received!\nTotal Number of Requests: ${++totalRequests}`);
  }).listen(port);
}

const input = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
});

input.question("Plese input the desired port number for the server run: ", (answer) => runServer(answer));