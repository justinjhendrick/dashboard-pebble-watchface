var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

function getRequest(url, onload, onerror) {
  // Learned from
  // * https://github.com/chrislewicki/A-Little-More/blob/main/src/pkjs/index.nokey.js
  // * https://github.com/Sichroteph/Weather-Graph/blob/master/src/pkjs/js/pebble_js_app.js
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { onload(this) };
  // FIXME not sure if onerror works?
  xhr.onerror = onerror;
  xhr.ontimeout = onerror;
  xhr.onabort = onerror;
  xhr.open("GET", url);
  xhr.timeout = 3000;
  xhr.setRequestHeader("User-Agent", "https://github.com/justinjhendrick/dashboard-pebble-watchface");
  xhr.send();
}

var INVALID_TEMP = 999;

var cache = {
  time: 0,
  temp_c: INVALID_TEMP,
}

function millis_from_mins(mins) {
  return mins * 60 * 1000;
}

function handleUnknownWeather() {
  console.log("Something went wrong. Sending INVALID_TEMP to watch");
  cache.time = Date.now();  // update time, even though we failed to avoid overloading their server
  cache.temp_c = INVALID_TEMP;
  Pebble.sendAppMessage(
    {weather_now_temp_c: INVALID_TEMP},
    function() {},
    function(e) { console.log('Error sending weather to Pebble: ' + e.error.message); }
  );
}

function locationSuccess(pos) {
  if (Date.now() > cache.time + millis_from_mins(30)) {
    // cached value is too old. attempt to update.
    var url =
      'https://api.met.no/weatherapi/locationforecast/2.0/compact'
      + '?lat=' + pos.coords.latitude.toFixed(1)
      + '&lon=' + pos.coords.longitude.toFixed(1);
    console.log('Fetching weather from ' + url);
    // TODO where is the error callback for this getRequest?
    getRequest(url, function(response) {
      if (response.status != 200) {
        console.log("Error code from remote server " + response.status);
        handleUnknownWeather();
        return;
      }
      var json = JSON.parse(response.responseText);
      var temperature_celsius = json.properties.timeseries[0].data.instant.details.air_temperature;
      console.log('Got Temp ' + temperature_celsius + 'C from remote server');
      cache.time = Date.now();
      cache.temp_c = temperature_celsius;
      Pebble.sendAppMessage(
        {weather_now_temp_c: Math.round(temperature_celsius)},
        function() {},
        function(e) { console.log('Error sending weather to Pebble: ' + e.error.message); }
      );
    }, handleUnknownWeather);
  } else {
    console.log('reusing cached weather from ' + cache.time);
    Pebble.sendAppMessage(
      {weather_now_temp_c: Math.round(cache.temp_c)},
      function() {},
      function(e) { console.log('Error sending weather to Pebble: ' + e.error.message); }
    );
  }
}

function locationError(err) {
  console.log('Error requesting location: ' + JSON.stringify(err));
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener('ready', function(e) {
  getWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  getWeather();
});