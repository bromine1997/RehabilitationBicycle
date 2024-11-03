var gateway = `ws://${window.location.hostname}:777/ws`;
var websocket;
window.addEventListener('load', onload);

var filetype;
var filenameBin;
var filenameTxt;

function onload(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log("Trying to open a WebSocket connection??");
    websocket = new WebSocket(gateway);
    websocket.binaryType = "arraybuffer";

    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function StartSubmit() {
	console.log('Send START command');
    websocket.send("START");
}

function StopSubmit() {
	console.log('Send STOP command');
    websocket.send("STOP");
}

function SaveSubmit() {
    const rbs = document.querySelectorAll('input[name="filetype"]');
    for (const rb of rbs) {
        if (rb.checked) {
            filetype = rb.value;
            break;
        }
    }
	console.log('Send SAVE command');
    websocket.send("SAVE");
}

var saveByteArray = ( function() {
	var b = document.createElement("a");
	document.body.appendChild(b);
	b.style = "display:none";
	return function( data, name ) {
		var blob = new Blob( data, {type:"octet/stream"} ), url = window.URL.createObjectURL( blob );
		b.href = url;
		b.download = name;
		b.click();
		window.URL.revokeObjectURL(url);
	};
}() );

var saveTextArray = ( function() {
	var t = document.createElement("a");
	document.body.appendChild(t);
	t.style = "display:none";
	return function( data, name ) {
		var blob = new Blob( data, {type:"text/plain"} ), url = window.URL.createObjectURL( blob );
		t.href = url;
		t.download = name;
		t.click();
		window.URL.revokeObjectURL(url);
	};
}() );


var _appendBuffer = function(buffer1, buffer2) {
	var tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength);
	tmp.set(new Uint8Array(buffer1), 0);
	tmp.set(new Uint8Array(buffer2), buffer1.byteLength)
	return tmp.buffer;
};

var totalBuffer = new Uint8Array(0);

function onMessage(event) {
	if ( event.data instanceof ArrayBuffer ) {
		var buffer = event.data;
		var bufferLength = buffer.byteLength;
		console.log(bufferLength);

		if ( bufferLength == 1 ) {
			var now = new Date();
			var dd = now.getDate() < 10 ? "0" + now.getDate() : now.getDate();
			var hh = now.getHours() < 10 ? "0" + now.getHours() : now.getHours();
			var mm = now.getMinutes() < 10 ? "0" + now.getMinutes() : now.getMinutes();
			var ss = now.getSeconds() < 10 ? "0" + now.getSeconds() : now.getSeconds();
			filenameBin = "Sensor_" + dd + "-" + hh + mm + ss + ".bin";
			filenameTxt = "Sensor_" + dd + "-" + hh + mm + ss + ".txt";

			console.log(filenameBin);

			if ( filetype == "TEXT" ) {
				var s = "";
				for ( let i = 0 ; i < totalBuffer.byteLength ; i += 20 ) {
					var sensor5view = new Int32Array(totalBuffer, i, 5);
					s = s + (sensor5view[0] + " " + sensor5view[1] + " " + sensor5view[2] + " " + sensor5view[3] + " " + sensor5view[4] + "\n");
				}
				saveTextArray([s], filenameTxt);
			} else {
				saveByteArray([totalBuffer], filenameBin);
			}
			totalBuffer = new Uint8Array(0);
		} else {
			totalBuffer = _appendBuffer(totalBuffer, buffer);
		}
	} else {
		console.log(event.data);
		var myObj = JSON.parse(event.data);
		var keys = Object.keys(myObj);

		document.getElementById("rarm").innerHTML = myObj[keys[0]];
		document.getElementById("larm").innerHTML = myObj[keys[1]];
		document.getElementById("rfoot").innerHTML = myObj[keys[2]];
		document.getElementById("lfoot").innerHTML = myObj[keys[3]];
		document.getElementById("angle").innerHTML = myObj[keys[4]];

		var pn = parseInt(myObj[keys[5]]);

		let minute = Math.floor(pn / 2400);
		let second = Math.floor(Math.floor(pn % 2400)/40);

		let minString = minute;
		let secString = second;

		if (minute < 10) {
			minString = "0" + minString;
		}
		if (second < 10) {
			secString = "0" + secString;
		}
		document.getElementById('min').innerHTML = minString;
		document.getElementById('sec').innerHTML = secString;
	}
}
