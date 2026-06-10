var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
var clay = new Clay(clayConfig);

function millis_from_mins(v) {
  return millis_from_secs(v * 60);
}

function millis_from_secs(v) {
  return v * 1000;
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
var weather_cache = {
  time: 0,
  temp_deci_c: INVALID_TEMP,
}
var location_cache = {
  lat: null,
  lon: null,
}

function handleUnknownWeather(e) {
  console.log("Something went wrong with weather API. " + JSON.stringify(e));
}

function getWeather() {
  var weather_cache_str = localStorage.getItem("weather_cache_v2")
  if (weather_cache_str != null) {
    weather_cache = JSON.parse(weather_cache_str);
  }

  if (
    Date.now() <= weather_cache.time + millis_from_mins(20)
    && weather_cache.temp_deci_c != INVALID_TEMP
  ) {
    console.log("reusing cached weather from " + weather_cache.time);
    Pebble.sendAppMessage(
      {weather_now_temp_deci_c: weather_cache.temp_deci_c},
      function() {},
      function(e) { console.log("Error sending weather to Pebble: " + e.error.message); }
    );
    return;
  }

  if (location_cache.lat == null || location_cache.lon == null) {
    var location_cache_str = localStorage.getItem("location_cache_v2")
    if (location_cache_str != null) {
      location_cache = JSON.parse(location_cache_str);
    }
  }
  if (location_cache.lat == null || location_cache.lon == null) {
    console.log("cannot get weather if we don't know where");
    return;
  }

  // weather_cached value is too old or invalid. attempt to update.
  var url =
    "https://api.met.no/weatherapi/locationforecast/2.0/compact"
    + "?lat=" + location_cache.lat
    + "&lon=" + location_cache.lon;
  console.log("Fetching weather from " + url);
  getRequest(url, function(response) {
    if (response.status != 200) {
      console.log("Error code from remote server " + response.status);
      return;
    }
    var json = JSON.parse(response.responseText);
    var temperature_celsius = json.properties.timeseries[0].data.instant.details.air_temperature;
    var temp_deci_c = Math.round(temperature_celsius * 10);
    console.log("Got Temp " + temperature_celsius + "C from remote server");
    weather_cache.time = Date.now();
    weather_cache.temp_deci_c = temp_deci_c;
    localStorage.setItem("weather_cache_v2", JSON.stringify(weather_cache))
    Pebble.sendAppMessage(
      {weather_now_temp_deci_c: temp_deci_c},
      function() {},
      function(e) { console.log("Error sending weather to Pebble: " + e.error.message); }
    );
  }, handleUnknownWeather);
}

function locationSuccess(pos) {
  location_cache.lat = pos.coords.latitude.toFixed(1)
  location_cache.lon = pos.coords.longitude.toFixed(1)
  localStorage.setItem("location_cache_v2", JSON.stringify(location_cache))
  getWeather();
}

function locationError(err) {
  console.log("Error requesting location: " + JSON.stringify(err));
  getWeather();
}

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    {
      timeout: millis_from_secs(15),
      maximumAge: millis_from_mins(2 * 60),
      enableHighAccuracy: false
    }
  );
}

Pebble.addEventListener("ready", function(e) {
  getLocation();
});

Pebble.addEventListener("appmessage", function(e) {
  // sending an empty message from watch to phone
  // is interpreted as "give me the current weather"
  getLocation();
});
