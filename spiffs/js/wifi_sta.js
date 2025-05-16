// 菜单点击处理
function menuClick(obj) {
  if (document.getElementById('leftMenu').style.display === 'block') {
    document.getElementById('leftMenu').style.display = 'none';
  } else {
    document.getElementById('leftMenu').style.display = 'block';
  }
}

// 初始化页面
function initPage() {
  // 获取当前网络模式
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/network_mode", true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        var data = JSON.parse(xhr.responseText);
        var modeText = data.mode === 0 ? "4G模式" : "WiFi STA模式";
        document.getElementById('network-mode').textContent = modeText;
      } else {
        document.getElementById('network-mode').textContent = "获取失败";
      }
    }
  };
  xhr.send();
}

// 保存WiFi配置
function saveWifiConfig() {
  var ssid = document.getElementById('ssid').value;
  var password = document.getElementById('password').value;
  
  if (!ssid) {
    showMessage("请输入WiFi SSID", false);
    return;
  }
  
  var xhr = new XMLHttpRequest();
  xhr.open("POST", "/wifi_sta", true);
  xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        showMessage("WiFi配置已保存并尝试连接", true);
      } else {
        showMessage("保存WiFi配置失败", false);
      }
    }
  };
  xhr.send("ssid=" + encodeURIComponent(ssid) + "&password=" + encodeURIComponent(password));
}

// 显示消息
function showMessage(message, isSuccess) {
  var messageElement = document.getElementById('result-message');
  messageElement.textContent = message;
  messageElement.style.display = 'block';
  
  if (isSuccess) {
    messageElement.className = 'message success-message';
  } else {
    messageElement.className = 'message error-message';
  }
  
  setTimeout(function() {
    messageElement.style.display = 'none';
  }, 5000);
}

// 初始化哈希变化处理
function initHash() {
  var hash = window.location.hash;
  if (hash === '#base' || hash === '') {
    // 默认处理，如果有需要的话
  } else if (hash === '#advance') {
    // 高级设置，如果有需要的话
  }
}

// 页面加载完成后初始化
window.onload = function() {
  initPage();
  
  // 添加哈希变化监听，与其他页面保持一致
  window.addEventListener('hashchange', function() {
    initHash();
  });
}; 