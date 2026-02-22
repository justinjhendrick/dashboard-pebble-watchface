var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

// Learned from https://github.com/chrislewicki/A-Little-More/blob/main/src/pkjs/index.nokey.js

function xhrRequest(url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { callback(this.responseText); };
  xhr.open(type, url);
  xhr.send();
}

function locationSuccess(pos) {
  var url =
    'https://api.met.no/weatherapi/locationforecast/2.0/compact'
    + '?lat=' + pos.coords.latitude
    + '&lon=' + pos.coords.longitude
  ;
  // TODO need to set user agent
  // Should I use 'fetch' instead of xhrRequest?

  console.log('Weather URL: ' + url);

  var dictionary = {
    weather_now_temp_c: 111.9,  // TODO get real temperature
  };
  Pebble.sendAppMessage(dictionary,
    function() { console.log('Weather sent to Pebble'); },
    function(e) { console.log('Error sending weather to Pebble: ' + e.error.message); }
  );

  // TODO caching
  // TODO actually send the request

  // xhrRequest(url, 'GET', function(responseText) {
  //   var json = JSON.parse(responseText);
  //   var temperatureC = Math.round(json.main.temp - 273.15);
  //   console.log('Temp: ' + temperatureC + 'C');
  //   var dictionary = {
  //     weather_now_temp_c: temperatureC,
  //   };
  //   Pebble.sendAppMessage(dictionary,
  //     function() { console.log('Weather sent to Pebble'); },
  //     function(e) { console.log('Error sending weather to Pebble: ' + e.error.message); }
  //   );
  // });
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
  console.log('PebbleKit JS ready');
  getWeather();
});

// If you send an AppMessage from the watch to trigger refresh, this will refetch.
Pebble.addEventListener('appmessage', function(e) {
  getWeather();
});