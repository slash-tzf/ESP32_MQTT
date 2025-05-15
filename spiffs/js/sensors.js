// const baseurl = 'http://192.168.4.1'
const baseurl = window.location.origin
console.log(baseurl)
const CONSTANT = {
    GET_SENSORS_URL: `${baseurl}/sensors/data`,
}

var getSensorsTimer = null;

var Ajax = {
    get: function(url, callback) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, false);
        xhr.setRequestHeader('Content-Type', 'application/json')
        xhr.onreadystatechange = function() {
            if (xhr.readyState === 4) {
                if (xhr.status === 200 || xhr.status === 304) {
                    console.log(xhr.responseText);
                    callback(xhr.responseText);
                } else {
                    console.log(xhr.responseText);
                }
            }
        }
        xhr.send();
    }
}

function showSensors() {
    console.log('显示传感器数据');
    var base1 = document.querySelector('.header-title-one');
    base1.style.color = '#000';
    base1.style.cursor = 'auto';
    var base = document.querySelector('.header-title-two');
    base.style.color = '#888888';
    base.style.cursor = 'pointer';
    var sensorsShow = document.getElementById('sensorsShow');
    sensorsShow.style.display = 'block';
    var gpsShow = document.getElementById('gpsShow');
    gpsShow.style.display = 'none';
}

function showGPS() {
    console.log('显示GPS数据');
    var base1 = document.querySelector('.header-title-one');
    base1.style.color = '#888888';
    base1.style.cursor = 'pointer';
    var base = document.querySelector('.header-title-two');
    base.style.color = '#000';
    base.style.cursor = 'auto';
    var sensorsShow = document.getElementById('sensorsShow');
    sensorsShow.style.display = 'none';
    var gpsShow = document.getElementById('gpsShow');
    gpsShow.style.display = 'block';
}

function updateSensorsData() {
    Ajax.get(CONSTANT.GET_SENSORS_URL, function(res) {
        res = JSON.parse(res);
        console.log('获取传感器信息：', res);

        // 更新传感器数据
        var temperature = document.getElementById('temperature');
        var humidity = document.getElementById('humidity');
        var lightIntensity = document.getElementById('light-intensity');
        var sensorsUpdateTime = document.getElementById('sensors-update-time');

        // 检查传感器数据有效性
        if (res.sensors_valid) {
            temperature.innerHTML = res.temperature.toFixed(1) + '°C';
            humidity.innerHTML = res.humidity.toFixed(1) + '%';
            lightIntensity.innerHTML = res.light_intensity.toFixed(1) + 'lx';
        } else {
            temperature.innerHTML = '--°C';
            humidity.innerHTML = '--%';
            lightIntensity.innerHTML = '--lx';
        }

        // 更新GPS数据
        var latitude = document.getElementById('latitude');
        var longitude = document.getElementById('longitude');
        var altitude = document.getElementById('altitude');
        var speed = document.getElementById('speed');
        var course = document.getElementById('course');
        var dataSource = document.getElementById('data-source');

        // 检查GPS数据有效性
        if (res.gps_valid) {
            latitude.innerHTML = res.latitude.toFixed(6) + '° ' + res.ns_indicator;
            longitude.innerHTML = res.longitude.toFixed(6) + '° ' + res.ew_indicator;
            altitude.innerHTML = res.altitude.toFixed(1) + '米';
            speed.innerHTML = res.speed.toFixed(1) + '节';
            course.innerHTML = res.course.toFixed(1) + '°';
            
            // 数据来源显示
            if (res.data_source === 0) {
                dataSource.innerHTML = 'GNSS模块';
            } else {
                dataSource.innerHTML = '基站定位';
            }
        } else {
            latitude.innerHTML = '--°';
            longitude.innerHTML = '--°';
            altitude.innerHTML = '--米';
            speed.innerHTML = '--节';
            course.innerHTML = '--°';
            dataSource.innerHTML = '--';
        }

        // 更新时间显示
        var date = new Date(res.timestamp * 1000);
        var timeString = date.getHours() + ':' + 
                          (date.getMinutes() < 10 ? '0' + date.getMinutes() : date.getMinutes()) + ':' + 
                          (date.getSeconds() < 10 ? '0' + date.getSeconds() : date.getSeconds());
        sensorsUpdateTime.innerHTML = timeString;
    });
}

function menuClick(e) {
    console.log('e: ', e.classList);
    var menuView = document.getElementById('leftMenu');
    if (e.classList.contains('delMenu')) {
        menuView.style.display = 'none';
        e.classList.remove('delMenu');
    } else {
        e.classList.add('delMenu');
        menuView.style.display = 'block';
    }
}

function initPage() {
    console.log('传感器页面初始化');
    
    // 开始定时更新数据
    updateSensorsData();
    getSensorsTimer = setInterval(updateSensorsData, 3000);
}

// 页面加载完成时执行初始化
window.onload = function() {
    initPage();
}; 