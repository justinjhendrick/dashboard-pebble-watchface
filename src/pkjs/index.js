var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
var clay = new Clay(clayConfig);

var ENABLE_CACHE = true;

function sendToWatch(d) {
  Pebble.sendAppMessage(
    d,
    function() {},
    function(e) { console.log("Error sending to Pebble: " + e.error.message); }
  );
}

function millis_from_secs(v) {
  return v * 1000;
}

function millis_from_mins(v) {
  return millis_from_secs(v * 60);
}

function millis_from_hours(v) {
  return millis_from_mins(v * 60);
}

function handleAbort(e) {
  console.log("GET Abort. " + JSON.stringify(e));
  sendToWatch({error_code: 1});
}

function handleError(e) {
  console.log("GET Error. " + JSON.stringify(e));
  sendToWatch({error_code: 2});
}

function handleTimeout(e) {
  console.log("GET Timeout. " + JSON.stringify(e));
  sendToWatch({error_code: 3});
}

function getRequest(url, onload) {
  // Learned from
  // * https://github.com/chrislewicki/A-Little-More/blob/main/src/pkjs/index.nokey.js
  // * https://github.com/Sichroteph/Weather-Graph/blob/master/src/pkjs/js/pebble_js_app.js
  var xhr = new XMLHttpRequest();
  xhr.addEventListener("load", function() { onload(this) });
  xhr.addEventListener("abort", handleAbort);
  xhr.addEventListener("error", handleError);
  xhr.addEventListener("timeout", handleTimeout);
  xhr.open("GET", url);
  xhr.timeout = millis_from_secs(15);
  xhr.setRequestHeader("User-Agent", "https://github.com/justinjhendrick/dashboard-pebble-watchface");
  xhr.send();
}

// These INVALIDS must match watch side definition
var INVALID_TEMP = 9999;
var INVALID_RAIN = -1;
var INVALID_TIME = 0;

var weather_cache = {
  time: 0,
  temp_deci_c: INVALID_TEMP,
  rain_1h_dmm: INVALID_RAIN,
  rain_6h_dmm: INVALID_RAIN,
}

var location_cache = {
  lat: null,
  lon: null,
}

var sun_cache = {
  time: 0,
  rise: INVALID_TIME,
  set: INVALID_TIME,
}

function getSun() {
  var now = Date.now();
  var sun_cache_str = localStorage.getItem("sun_cache");
  if (sun_cache_str != null) {
    sun_cache = JSON.parse(sun_cache_str);
  }
  if (ENABLE_CACHE && now <= sun_cache.time + millis_from_hours(24)) {
    console.log("resending cached sun");
    sendToWatch(
      {
        sunrise: sun_cache.rise,
        sunset: sun_cache.set,
      }
    );
    return;
  }
  if (location_cache.lat == null || location_cache.lon == null) {
    console.log("cannot get sunrise/sunset if we don't know where");
    sendToWatch({error_code: 4});
    return;
  }
  var url =
    "https://api.met.no/weatherapi/sunrise/3.0/sun"
    + "?lat=" + location_cache.lat
    + "&lon=" + location_cache.lon;
  console.log("Fetching sun from " + url);
  getRequest(url, function(response) {
    if (response.status < 200 || response.status >= 300) {
      console.log("Error code from sun " + response.status);
      sendToWatch({error_code: response.status});
      return;
    }
    var json = JSON.parse(response.responseText);
    sun_cache.time = now;
    sun_cache.rise = Math.round(new Date(json.properties.sunrise.time).valueOf() / 1000);
    sun_cache.set = Math.round(new Date(json.properties.sunset.time).valueOf() / 1000);
    localStorage.setItem("sun_cache", JSON.stringify(sun_cache))
    sendToWatch(
      {
        sunrise: sun_cache.rise,
        sunset: sun_cache.set,
      }
    );
  });
}

function findNearest(timeseries) {
  const now = new Date();
  var nearest_point = null;
  var min_diff = null;
  for (const point of timeseries) {
    const t = Date.parse(point.time);
    const diff = Math.abs(t.valueOf() - now.valueOf());
    if (min_diff == null || diff < min_diff) {
      min_diff = diff;
      nearest_point = point;
    }
  }
  return nearest_point;
}

function getWeather() {
  var weather_cache_str = localStorage.getItem("weather_cache_v2")
  if (weather_cache_str != null) {
    weather_cache = JSON.parse(weather_cache_str);
  }

  if (
    ENABLE_CACHE
    && Date.now() <= weather_cache.time + millis_from_mins(20)
    && weather_cache.temp_deci_c != INVALID_TEMP
  ) {
    console.log("resending cached weather");
    sendToWatch(
      {
        weather_now_temp_deci_c: weather_cache.temp_deci_c,
        weather_rain_1h_dmm: weather_cache.rain_1h_dmm,
        weather_rain_6h_dmm: weather_cache.rain_6h_dmm,
      }
    );
    return;
  }

  if (location_cache.lat == null || location_cache.lon == null) {
    console.log("cannot get weather if we don't know where");
    sendToWatch({error_code: 5});
    return;
  }

  // weather_cached value is too old or invalid. attempt to update.
  var url =
    "https://api.met.no/weatherapi/locationforecast/2.0/compact"
    + "?lat=" + location_cache.lat
    + "&lon=" + location_cache.lon;
  console.log("Fetching weather from " + url);
  getRequest(url, function(response) {
    if (response.status < 200 || response.status >= 300) {
      console.log("Error code from weather " + response.status);
      sendToWatch({error_code: response.status});
      return;
    }
    var json = JSON.parse(response.responseText);
    var point = findNearest(json.properties.timeseries);
    var temperature_celsius = point.data.instant.details.air_temperature;
    var rain_1h_dmm = Math.round(point.data.next_1_hours.details.precipitation_amount * 10);
    var rain_6h_dmm = Math.round(point.data.next_6_hours.details.precipitation_amount * 10);
    var temp_deci_c = Math.round(temperature_celsius * 10);
    console.log("Got Temp " + temperature_celsius + "C from remote server");
    weather_cache.time = Date.now();
    weather_cache.temp_deci_c = temp_deci_c;
    weather_cache.rain_1h_dmm = rain_1h_dmm;
    weather_cache.rain_6h_dmm = rain_6h_dmm;
    localStorage.setItem("weather_cache_v2", JSON.stringify(weather_cache))
    sendToWatch(
      {
        weather_now_temp_deci_c: weather_cache.temp_deci_c,
        weather_rain_1h_dmm: weather_cache.rain_1h_dmm,
        weather_rain_6h_dmm: weather_cache.rain_6h_dmm,
      }
    );
  });
}

function locationSuccess(pos) {
  location_cache.lat = pos.coords.latitude.toFixed(1)
  location_cache.lon = pos.coords.longitude.toFixed(1)
  localStorage.setItem("location_cache_v2", JSON.stringify(location_cache))
  getWeather();
  getSun();
}

function locationError(err) {
  console.log("Error requesting location: " + JSON.stringify(err));
  if (location_cache.lat == null || location_cache.lon == null) {
    var location_cache_str = localStorage.getItem("location_cache_v2")
    if (location_cache_str != null) {
      location_cache = JSON.parse(location_cache_str);
    }
  }
  getWeather();
  getSun();
}

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    {
      timeout: millis_from_secs(15),
      maximumAge: millis_from_hours(2),
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
