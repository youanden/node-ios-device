var iosDevice = require('../ios-device');

var off = iosDevice.trackDevices(function (err, devices) {
	if (err) {
		console.error(err.toString());
		process.exit(1);
	}
	console.log(devices);
}, 1000);

// off();
