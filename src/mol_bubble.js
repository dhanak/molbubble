var stations = [];

// for debugging with jsfiddle.net
/*
var Pebble = {
    "addEventListener": function(evt, listener)
    {
        if (evt == "ready")
            listener(0);
    },
    "sendAppMessage": function(msg, ack, nack)
    {
        console.log(msg);
        ack(0);
    }
};
*/

// distance calculator
var DistanceCalculator = function(coords)
{
    var equatR = 6378.1370;
    var polarR = 6356.7523;
    
    this.lat = this.degToRad(coords.latitude);
    this.lon = this.degToRad(coords.longitude);
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
    var lat2 = this.degToRad(coords.latitude);
    var lon2 = this.degToRad(coords.longitude);
    var dlat = this.lat - lat2;
    var dlon = this.lon - lon2;
    return { "x": Math.round(this.R*this.cos*dlon*1000), "y": Math.round(this.R*dlat*1000) };
};
var dc;

// station publisher sends station names and locations over to Pebble, one by one
var StationPublisher = function() {};
StationPublisher.prototype.sent = function(e)
{
    console.log("Station info sent to Pebble successfully!");
    this.next++;
    this.sendNext();
};
StationPublisher.prototype.error = function(e)
{
    console.log("Sending station info to Pebble failed, retrying!");
    this.sendNext();
};
StationPublisher.prototype.sendNext = function()
{
    if (this.next < stations.length)
    {
        var station = stations[this.next];
        var pos = dc.toSquare(station);
        Pebble.sendAppMessage({
            "index": this.next,
            "name": station.name,
            "x": pos.x,
            "y": pos.y,
            "racks": station.racks
        }, this.sent.bind(this), this.error.bind(this));
    }
};
StationPublisher.prototype.publish = function()
{
    this.next = -1; // sent() will set it to 0
    Pebble.sendAppMessage({ "num_stations": stations.length },
        this.sent.bind(this), this.publish.bind(this));
};
var stationPublisher = new StationPublisher();

// station updater sends updated station information to Pebble in reasonable chunks
var StationUpdater = function()
{
    this.chunkSize = 120;
};
StationUpdater.prototype.sent = function(e)
{
    console.log("Station data sent to Pebble successfully!");
    this.next += this.chunkSize;
    this.sendNextChunk();
};
StationUpdater.prototype.error = function(e)
{
    console.log("Sending station data to Pebble failed, retrying!");
    this.sendNextChunk();
    
};
StationUpdater.prototype.sendNextChunk = function()
{
    if (this.next < stations.length)
    {
        var update = [ this.next ];
        var to = Math.min(stations.length, this.next + this.chunkSize);
        for (var i = this.next; i < to; i++)
        {
            var station = stations[i];
            update.push(station.bikes);
        }
        Pebble.sendAppMessage({ "update": update }, this.sent.bind(this), this.error.bind(this));
    }
};
StationUpdater.prototype.publish = function()
{
    this.next = -this.chunkSize; // sent() will set it to 0
    Pebble.sendAppMessage({ "num_stations": stations.length },
        this.sent.bind(this), this.publish.bind(this));
};
var stationUpdater = new StationUpdater();

// location updater
var LocationUpdater = function() {};
LocationUpdater.prototype.recieved = function(pos)
{
    console.log("Recieved updated coordinates!");
    if (this.coords != pos.coords)
    {
        this.coords = pos.coords;
        this.publish();
    }
};
LocationUpdater.prototype.error = function(err)
{
    console.log("Error recieving updated coordinates!");
};
LocationUpdater.prototype.sent = function(e)
{
    console.log("Position data sent to Pebble successfully!");
};
LocationUpdater.prototype.send = function(pos)
{
    Pebble.sendAppMessage(pos, this.sent.bind(this), this.send.bind(this, pos));
};
LocationUpdater.prototype.publish = function()
{
    if (dc && this.coords)
    {
        var xy = dc.toSquare(this.coords);
        this.send(xy);
    }
};
LocationUpdater.prototype.subscribe = function()
{
    navigator.geolocation.watchPosition(
        this.recieved.bind(this), this.error.bind(this),
        {timeout: 15000, maximumAge: 60000}
    );
};
var locationUpdater = new LocationUpdater();

// station list updater
var DataLoader = function() {};
DataLoader.prototype.xhrRequest = function(url, type, callback)
{
  var xhr = new XMLHttpRequest();
  xhr.onload = function () { callback(this.responseXML); };
  xhr.open(type, url);
  xhr.send();
};
DataLoader.prototype.update = function(publish)
{
    this.xhrRequest("https://nextbike.net/maps/nextbike-live.xml?&domains=mb", 'GET', function(responseXML)
    {
        stations = [];
        var cities = responseXML.getElementsByTagName("city");
        for (var i = 0; i < cities.length; i++)
        {
            var city = cities[i];
            if (city.getAttribute("name") != "Budapest")
                continue;

            var coords = {
                "latitude": parseFloat(city.getAttribute("lat")),
                "longitude": parseFloat(city.getAttribute("lng"))
            };
            dc = new DistanceCalculator(coords);
            locationUpdater.publish();

            var places = city.getElementsByTagName("place");
            for (var j = 0; j < places.length; j++)
            {
                var place = places[j];
                stations.push({
                    "uid": parseInt(place.getAttribute("uid")),
                    "name": place.getAttribute("name").slice(5),
                    "latitude": place.getAttribute("lat"),
                    "longitude": place.getAttribute("lng"),
                    "bikes": parseInt(place.getAttribute("bikes")),
                    "racks": parseInt(place.getAttribute("bike_racks"))
                });
            }
            break;
        }
        console.log("Collected data for " + stations.length + " stations from nextbike.net");
        stations.sort(function(a,b) { return a.uid - b.uid; });
        stationUpdater.publish();
        if (publish)
        {
            stationPublisher.publish();
        }
    });
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
    dataLoader.update(false);
});
