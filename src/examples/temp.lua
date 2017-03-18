pin=1
gpio.mode(pin, gpio.OUTPUT)
print(gpio.read(pin))
gpio.write(pin, gpio.HIGH)
print(gpio.read(pin))
gpio.write(pin, gpio.LOW)
print(gpio.read(pin))
gpio.write(pin, gpio.HIGH)
