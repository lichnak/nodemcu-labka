-- File name: daytime.lua
-- tested on NodeMCU 0.9.5 build 20150126
-- adapted from httpget.lua (example from LuaLoader Quick Start guide)
-- Queries NIST DayTime server for a date time string
-- Fixed format returned:
-- JJJJJ YR-MO-DA HH:MM:SS TT L H msADV UTC(NIST) OTM. 
-- Ex: 57053 15-01-31 20:14:43 00 0 0 703.0 UTC(NIST) *
--   JJJJJ - the modified Julian Date ( MJD is the Julian Date              minus 2400000.5)
--   YR-MO-DA - the Date
--   HH:MM:SS - the Time 
--   TT -  USA is on Standard Time (ST) or (DST) : 00 (ST)
--   L - Leap second at the end of the month (0: no, 1: +1, 2: -1)
--   H - Health of the server (0: healthy, >0: errors)
--   msADV - time code advanced in ms for network delays
--   UTC(NIST) - the originator of this time code
--   OTM - on-time marker : * (time correct on arival)

print('DayTime.lua started')
conn = nil
conn=net.createConnection(net.TCP, 0) 
-- show the server returned payload
conn:on("receive", function(conn, payload) 
                       print('\nreceived')
                       print(payload)
                       print('extracted Date and Time')
                       print('20'..string.sub(payload,8,14)..
                              string.sub(payload,15,24))
                   end) 
-- show disconnection
conn:on("disconnection", function(conn, payload) print('\nDisconnected') end)
--connect                                   
conn:connect(13,'utcnist2.colorado.edu') 