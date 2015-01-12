function xhrRequest(url, type, callback)
{
  var xhr = new XMLHttpRequest();
  xhr.onload = function () { callback(this.responseXML); };
  xhr.open(type, url);
  xhr.send();
}

var DistanceCalculator = function()
{
    this.R = 6371.009; // default radius
    this.lat = this.lon = this.cos = NaN;
};
DistanceCalculator.prototype.degToRad = function(deg)
{
    return deg*Math.PI/180;
};
DistanceCalculator.prototype.radToDeg = function(rad)
{
    return Math.round(rad*180/Math.PI);
};
DistanceCalculator.prototype.setup = function(coords)
{
    var equatR = 6378.1370;
    var polarR = 6356.7523;
    
    this.lat = this.degToRad(coords.latitude);
    this.lon = this.degToRad(coords.longitude);
    this.sin = Math.sin(this.lat);
    this.cos = Math.cos(this.lat);

    // https://en.wikipedia.org/wiki/Earth_radius#Geocentric_radius
    var eec = equatR*equatR*this.cos;
    var pps = polarR*polarR*this.sin;
    this.R = Math.sqrt((eec*eec + pps*pps) / (eec*this.cos + pps*this.sin));
};
DistanceCalculator.prototype.polar = function(coords)
{
    // https://en.wikipedia.org/wiki/Geographical_distance#Spherical_Earth_projected_to_a_plane
    var lat2 = this.degToRad(coords.lat);
    var lon2 = this.degToRad(coords.lon);
    var dlat = this.lat - lat2;
    var dlon = this.lon - lon2;
    var cos_dlon = this.cos * dlon;
    var dist = Math.round(this.R * Math.sqrt(dlat*dlat + cos_dlon*cos_dlon) * 1000);
    // http://www.movable-type.co.uk/scripts/latlong.html
    var x = this.cos*Math.sin(lat2) - this.sin*Math.cos(lat2)*Math.cos(dlon);
    var y = Math.sin(dlon) * Math.cos(lat2);
    var bearing = Math.atan2(y,x);
    return { "r": dist, "a": bearing };
};

var stations = [];
var stations_to_send = 0;
var next_station_to_send = 0;
var selected_uid = 0;
var station_list_dirty = false;
var dc = new DistanceCalculator();

function appMessageSent(e)
{
    console.log("Station sent to Pebble successfully!");
    if (selected_uid === 0)
    {
        next_station_to_send++;
        sendNextStation();
    }
}

function appMessageError(e)
{
    console.log("Error sending info to Pebble!");
    sendNextStation();
}

function sendNextStation()
{
    if (next_station_to_send < stations_to_send)
    {
        var station = stations[next_station_to_send];
        Pebble.sendAppMessage({
            "uid": station.uid,
            "name": station.name,
            "distance": station.dist,
            "heading": dc.radToDeg(station.heading),
            "bikes": station.bikes,
            "racks": station.racks
        }, appMessageSent, appMessageError);
    }
}

function updateStations()
{
    if (stations.length === 0 || isNaN(dc.lat) || isNaN(dc.lon))
        return;
    
    next_station_to_send = -1;
    for (var i = 0; i < stations.length; i++)
    {
        var station = stations[i];
        var polar = dc.polar(station);
        station.dist = polar.r;
        station.heading = polar.a;
        if (station.uid == selected_uid)
        {
            next_station_to_send = i;
        }
    }

    if (next_station_to_send == -1)
    {   // send all stations
        stations.sort(function (a,b) { return a.dist - b.dist; });
        stations_to_send = Math.min(stations.length, 10);
        Pebble.sendAppMessage({"num_stations":stations_to_send}, appMessageSent);
        station_list_dirty = false;
    }
    else
    {   // send distance and heading for selected station
        sendNextStation();
        station_list_dirty = true;
    }
}

function locationSuccess(pos)
{
    dc.setup(pos.coords);
    console.log("Recieved updated coordinates");
    updateStations();
}

function locationError(err)
{
    console.log("Error requesting location!");
}

function fetchStationList()
{
    xhrRequest("https://nextbike.net/maps/nextbike-live.xml?&domains=mb", 'GET', function(responseXML)
    {
        stations = [];
        var cities = responseXML.getElementsByTagName("city");
        for (var i = 0; i < cities.length; i++)
        {
            var city = cities[i];
            if (city.getAttribute("name") != "Budapest")
                continue;
            var places = city.getElementsByTagName("place");
            for (var j = 0; j < places.length; j++)
            {
                var place = places[j];
                stations.push({
                    "uid": parseInt(place.getAttribute("uid")),
                    "name": place.getAttribute("name").slice(5),
                    "lat": parseFloat(place.getAttribute("lat")),
                    "lon": parseFloat(place.getAttribute("lng")),
                    "bikes": parseInt(place.getAttribute("bikes")),
                    "racks": parseInt(place.getAttribute("bike_racks"))
                });
           }
        }
        console.log("Collected data for " + stations.length + " stations from nextbike.net");
        updateStations();
    });
}

// Listen for when the watchface is opened
Pebble.addEventListener('ready', function(e)
{
    console.log("PebbleKit JS ready!");
    navigator.geolocation.watchPosition(locationSuccess, locationError, {timeout: 15000, maximumAge: 60000});
    fetchStationList();
});

// Listen for when an AppMessage is received
Pebble.addEventListener('appmessage', function(e)
{
    console.log("AppMessage received!");
    if (e.payload.uid === 0 && selected_uid === 0)
    {   // refresh station list
        fetchStationList();
    }
    else
    {   // select or deselect station
        selected_uid = e.payload.uid;
        if (selected_uid === 0 && station_list_dirty)
        {
            updateStations();
        }
    }
});
