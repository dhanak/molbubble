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
DistanceCalculator.prototype.setup = function(coords)
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
DistanceCalculator.prototype.distance = function(coords)
{
    // https://en.wikipedia.org/wiki/Geographical_distance#Spherical_Earth_projected_to_a_plane
    var dlat = this.lat - this.degToRad(coords.lat);
    var dlon = this.lon - this.degToRad(coords.lon);
    var cos_dlon = this.cos * dlon;
    return Math.round(this.R * Math.sqrt(dlat*dlat + cos_dlon*cos_dlon) * 1000);
};

var stations = [];
var dc = new DistanceCalculator();

function appMessageSent(e)
{
    console.log("Station sent to Pebble successfully!");
}

function appMessageError(e)
{
     console.log("Error sending weather info to Pebble!");
}

function updateStations()
{
    if (stations.length === 0 || isNaN(dc.lat) || isNaN(dc.lon))
        return;
    
    var i;
    var station;
    for (i = 0; i < stations.length; i++)
    {
        station = stations[i];
        station.dist = dc.distance(station);
    }
    stations.sort(function (a,b) { return a.dist - b.dist; });

    for (i = 0; i < Math.min(stations.length,10); i++)
    {
        station = stations[i];
        Pebble.sendAppMessage({
            "name": station.name,
            "distance": station.dist,
            "bikes": station.bikes,
            "racks": station.racks
        }, appMessageSent, appMessageError);
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
                    "name": place.getAttribute("name").slice(5),
                    "lat": place.getAttribute("lat"),
                    "lon": place.getAttribute("lng"),
                    "bikes": place.getAttribute("bikes"),
                    "racks": place.getAttribute("bike_racks")
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
    fetchStationList();
});
