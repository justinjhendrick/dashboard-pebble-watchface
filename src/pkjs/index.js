var Clay = require("pebble-clay");
var clayConfig = require("./config.json");
var clay = new Clay(clayConfig);

function millis_from_mins(mins) {
  return mins * 60 * 1000;
}

function getRequest(url, onload, onerror) {
  // Learned from
  // * https://github.com/chrislewicki/A-Little-More/blob/main/src/pkjs/index.nokey.js
  // * https://github.com/Sichroteph/Weather-Graph/blob/master/src/pkjs/js/pebble_js_app.js
  var xhr = new XMLHttpRequest();
  xhr.addEventListener("load", function() { onload(this) });
  // TODO: more testing on these error handlers
  xhr.addEventListener("abort", onerror);
  xhr.addEventListener("error", onerror);
  xhr.addEventListener("timeout", onerror);
  xhr.open("GET", url);
  xhr.timeout = 3000; // ms. but seems to have no effect?
  xhr.setRequestHeader("User-Agent", "https://github.com/justinjhendrick/dashboard-pebble-watchface");
  xhr.send();
}

var INVALID_TEMP = 9999; // must match watch side definition
var cache = {
  time: 0,
  temp_deci_c: INVALID_TEMP,
}

function handleUnknownWeather(event) {
  console.log("Something went wrong. Sending INVALID_TEMP to watch");
  cache.time = Date.now();  // update time, even though we failed. To avoid overloading their server even if this code is buggy
  cache.temp_deci_c = INVALID_TEMP;
  Pebble.sendAppMessage(
    {weather_now_temp_deci_c: INVALID_TEMP},
    function() {},
    function(e) { console.log("Error sending weather to Pebble: " + e.error.message); }
  );
}

var LAT_OVERRIDE = null; // '47.6';
var LON_OVERRIDE = null; // '-122.2';

function locationSuccess(pos) {
  if (Date.now() > cache.time + millis_from_mins(20)) {
    // cached value is too old. attempt to update.
    if (LAT_OVERRIDE != null) {
      lat = LAT_OVERRIDE;
    } else {
      lat = pos.coords.latitude.toFixed(1)
    }
    if (LON_OVERRIDE != null) {
      lon = LON_OVERRIDE;
    } else {
      lon = pos.coords.longitude.toFixed(1)
    }
    var url =
      "https://api.met.no/weatherapi/locationforecast/2.0/compact"
      + "?lat=" + lat
      + "&lon=" + lon;
    console.log("Fetching weather from " + url);
    getRequest(url, function(response) {
      if (response.status != 200) {
        console.log("Error code from remote server " + response.status);
        handleUnknownWeather(null);
        return;
      }
      var json = JSON.parse(response.responseText);
      var temperature_celsius = json.properties.timeseries[0].data.instant.details.air_temperature;
      var temp_deci_c = Math.round(temperature_celsius * 10);
      console.log("Got Temp " + temperature_celsius + "C from remote server");
      cache.time = Date.now();
      cache.temp_deci_c = temp_deci_c;
      Pebble.sendAppMessage(
        {weather_now_temp_deci_c: temp_deci_c},
        function() {},
        function(e) { console.log("Error sending weather to Pebble: " + e.error.message); }
      );
    }, handleUnknownWeather);
  } else {
    console.log("reusing cached weather from " + cache.time);
    Pebble.sendAppMessage(
      {weather_now_temp_deci_c: cache.temp_deci_c},
      function() {},
      function(e) { console.log("Error sending weather to Pebble: " + e.error.message); }
    );
  }
}

function locationError(err) {
  console.log("Error requesting location: " + JSON.stringify(err));
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener("ready", function(e) {
  getWeather();
});

Pebble.addEventListener("appmessage", function(e) {
  // sending an empty message from watch to phone
  // is interpreted as "give me the current weather"
  getWeather();
});