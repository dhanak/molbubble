/* // for debugging with jsfiddle.net
var cnt = 0;
var Pebble = {
    "addEventListener": function(evt, listener)
    {
        if (evt == "ready")
            listener(0);
    },
    "sendAppMessage": function(msg, ack, nack)
    {
        if (cnt++ % 5)
        {
            console.log(msg);
            ack(0);
        }
        else
        {
            nack(0);
        }
    },
    "showSimpleNotificationOnPebble": function(title, text)
    {
        alert(title + ": " + text);
    }
};
*/

var MessageQueue = function()
{
	this.queue = [];
	this.sending = false;
	
	this.MAX_RETRY  = 5;
	this.ACK_DELAY  = 0;
	this.NACK_DELAY = 200;
	this.TIMEOUT    = 1000;
};
MessageQueue.prototype.sendAppMessage = function(message, type, highPrio)
{
    this.queue[(highPrio ? "unshift" : "push")]({
		message: message,
		type: type,
		attempts: 0
    });
	if (!this.sending)
	{
		this.sendNext();
	}
};
MessageQueue.prototype.sendNext = function()
{
	this.sending = true;
	var message = this.queue.shift();
    if (!message)
	{
		this.sending = false;
		return;
	}

    message.attempts += 1;
	
	var mq = this;
	var timer = setTimeout(function() {
		console.log("Sending " + message.type + " timed out!");
		mq.sendNext();
	}, this.TIMEOUT);
	
    Pebble.sendAppMessage(message.message,
	function() // ack
	{
		clearTimeout(timer);
		console.log("Sending " + message.type + " succeeded!");
		setTimeout(function() { mq.sendNext(); }, mq.ACK_DELAY);
	},
	function() // nack
	{
		clearTimeout(timer);
		if (message.attempts < mq.MAX_RETRY)
		{
			mq.queue.unshift(message);
			setTimeout(function() { mq.sendNext(); }, mq.NACK_DELAY*message.attempts);
		}
		else
		{
			console.log("Giving up on sending " + message.type + "!");
		}
	});
};
var msgQueue = new MessageQueue();

// distance calculator
var DistanceCalculator = function(coords)
{
    var equatR = 6378.1370;
    var polarR = 6356.7523;
    
    this.lat = this.degToRad(coords.latitude || coords.lat);
    this.lon = this.degToRad(coords.longitude || coords.lon);
    var  sin = Math.sin(this.lat);
    this.cos = Math.cos(this.lat);

    // https://en.wikipedia.org/wiki/Earth_radius#Geocentric_radius
    var eec = equatR*equatR*this.cos;
    var pps = polarR*polarR*sin;
    this.R = Math.sqrt((eec*eec + pps*pps) / (eec*this.cos + pps*sin));
};
DistanceCalculator.prototype.degToRad = function(deg)
{
    return deg*Math.PI/180;
};
DistanceCalculator.prototype.toSquare = function(coords)
{   // https://en.wikipedia.org/wiki/Geographical_distance#Spherical_Earth_projected_to_a_plane
    var lat2 = this.degToRad(coords.latitude || coords.lat);
    var lon2 = this.degToRad(coords.longitude || coords.lon);
    var dlat = this.lat - lat2;
    var dlon = this.lon - lon2;
    return { "x": Math.round(this.R*this.cos*dlon*1000), "y": Math.round(this.R*dlat*1000) };
};
var dc = new DistanceCalculator({ "latitude":47.4925, "longitude":19.0514 }); // Budapest city center

// location updater
var LocationUpdater = function() {};
LocationUpdater.prototype.received = function(pos)
{
    console.log("received updated coordinates!");
    msgQueue.sendAppMessage(dc.toSquare(pos.coords), "position", true);
};
LocationUpdater.prototype.error = function(err)
{
    console.log("Error recieving updated coordinates!");
};
LocationUpdater.prototype.subscribe = function()
{
    navigator.geolocation.watchPosition(
        this.received.bind(this), this.error.bind(this),
        {TIMEOUT: 15000, maximumAge: 60000}
    );
};
var locationUpdater = new LocationUpdater();

// station list updater
var DataLoader = function()
{
	this.stations = [];
};
DataLoader.prototype.xhrRequest = function(url, type, callback)
{
    var xhr = new XMLHttpRequest();
    xhr.onload  = function() { callback(xhr.responseText); };
    xhr.onerror = function() { Pebble.showSimpleNotificationOnPebble("Error", "Failed to load data from server!"); };
    xhr.open(type, url);
    xhr.send();
};
DataLoader.prototype.sendStationCount = function()
{
    msgQueue.sendAppMessage({ "num_stations": this.stations.length }, "station count");
};
DataLoader.prototype.publishStations = function()
{
    for (var i = 0; i < this.stations.length; i++)
    {
        var station = this.stations[i];
        var pos = dc.toSquare(station);
        msgQueue.sendAppMessage({
            "index": i,
            "name":  station.name,
            "x":     pos.x,
            "y":     pos.y,
            "racks": station.spaces
        }, "station #" + i);
    }
};
DataLoader.prototype.updateStations = function()
{
    var chunkSize = 120;
    for (var i = 0; i < this.stations.length; i += chunkSize)
    {
        var update = [ i ];
        var to = Math.min(this.stations.length, i + chunkSize);
        for (var j = i; j < to; j++)
        {
            var station = this.stations[j];
            update.push(station.bikes);
        }
        msgQueue.sendAppMessage({ "update": update }, "update #" + (i/chunkSize));
    }
};
DataLoader.prototype.update = function(first)
{
    this.xhrRequest("http://futar.bkk.hu/bkk-utvonaltervezo-api/ws/otp/api/where/bicycle-rental.json", 'GET', function(responseText)
    {
        var json = JSON.parse(responseText);
        this.stations = json.data.list;
        this.stations.sort(function(a,b) { return a.id - b.id; });
        console.log("Collected data for " + this.stations.length + " stations from futar.bkk.hu");
        if (first) this.sendStationCount();
        this.updateStations();
        if (first) this.publishStations();
    }.bind(this));
};
var dataLoader = new DataLoader();

Pebble.addEventListener('ready', function(e)
{
    console.log("PebbleKit JS ready!");
    locationUpdater.subscribe();
    dataLoader.update(true);
});

Pebble.addEventListener('appmessage', function(e)
{
    console.log("AppMessage received!");
    dataLoader.update();
});
