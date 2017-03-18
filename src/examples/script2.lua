a = 0

function receive(conn, payload)
    print(payload) 
    a = a + 1

    local content="<!DOCTYPE html><html><head><link rel='icon' type='image/png' href='http://nodemcu.com/favicon.png' /></head><body><h1>Hello!</h1><p>Since the start of the server " .. a .. " connections were made</p></body></html>"
    local contentLength=string.len(content)

    conn:on("sent", function(sck) sck:close() end)
    conn:send("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length:" .. contentLength .. "\r\n\r\n" .. content)
end

function connection(conn) 
    conn:on("receive", receive)
end

srv=net.createServer(net.TCP, 1) 
srv:listen(8080, connection)