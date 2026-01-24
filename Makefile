SERIALPORT=/dev/ttyACM0

buildidf:
	idf.py build

flash:
	idf.py flash -p ${SERIALPORT}

erase:
	idf.py erase-flash -p ${SERIALPORT}

monflash:
	idf.py flash monitor -p ${SERIALPORT}

monitor:
	idf.py monitor -p ${SERIALPORT}

menuconfig:
	idf.py menuconfig

clean:
	idf.py clean
