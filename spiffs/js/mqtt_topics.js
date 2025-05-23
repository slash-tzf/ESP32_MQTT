// 基础URL
const baseurl = window.location.origin;
console.log(baseurl);

// API常量
const CONSTANT = {
    GET_TOPICS_URL: `${baseurl}/api/mqtt/topics`,
    ADD_TOPIC_URL: `${baseurl}/api/mqtt/topics/add`,
    DELETE_TOPIC_URL: `${baseurl}/api/mqtt/topics/delete`,
    PUBLISH_MESSAGE_URL: `${baseurl}/api/mqtt/publish`,
    GET_MQTT_SETTINGS_URL: `${baseurl}/api/mqtt/settings`,
    SAVE_MQTT_SETTINGS_URL: `${baseurl}/api/mqtt/settings/save`,
    GET_MQTT_STATUS_URL: `${baseurl}/api/mqtt/status`,
};

// MQTT状态检查定时器
let mqttStatusCheckTimer = null;
// MQTT状态
let currentMqttStatus = -1;

// AJAX请求方法
var Ajax = {
    get: function(url, callback) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.onreadystatechange = function() {
            if (xhr.readyState === 4) {
                if (xhr.status === 200 || xhr.status === 304) {
                    console.log(xhr.responseText);
                    callback(xhr.responseText);
                } else {
                    console.log('请求失败', xhr.status);
                }
            }
        };
        xhr.send();
    },
    
    post: function(url, data, callback) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.onreadystatechange = function() {
            if (xhr.readyState === 4) {
                if (xhr.status === 200 || xhr.status === 304) {
                    console.log(xhr.responseText);
                    callback(xhr.responseText);
                } else {
                    console.log('请求失败', xhr.status);
                }
            }
        };
        xhr.send(JSON.stringify(data));
    }
};

// 显示订阅主题面板
function showSubscribeTopics() {
    console.log('显示订阅主题面板');
    
    // 更新选项卡状态
    document.getElementById('tabSubscribe').classList.add('active');
    document.getElementById('tabPublish').classList.remove('active');
    document.getElementById('tabSettings').classList.remove('active');
    
    // 显示/隐藏相应面板
    var subscribePanel = document.getElementById('subscribeTopicsPanel');
    subscribePanel.style.display = 'block';
    var publishPanel = document.getElementById('publishPanel');
    publishPanel.style.display = 'none';
    var settingsPanel = document.getElementById('settingsPanel');
    settingsPanel.style.display = 'none';
    
    // 获取主题列表
    getTopicsList();
}

// 显示发布消息面板
function showPublishPanel() {
    console.log('显示发布消息面板');
    
    // 更新选项卡状态
    document.getElementById('tabSubscribe').classList.remove('active');
    document.getElementById('tabPublish').classList.add('active');
    document.getElementById('tabSettings').classList.remove('active');
    
    // 显示/隐藏相应面板
    var subscribePanel = document.getElementById('subscribeTopicsPanel');
    subscribePanel.style.display = 'none';
    var publishPanel = document.getElementById('publishPanel');
    publishPanel.style.display = 'block';
    var settingsPanel = document.getElementById('settingsPanel');
    settingsPanel.style.display = 'none';
}

// 显示MQTT设置面板
function showSettingsPanel() {
    console.log('显示MQTT设置面板');
    
    // 更新选项卡状态
    document.getElementById('tabSubscribe').classList.remove('active');
    document.getElementById('tabPublish').classList.remove('active');
    document.getElementById('tabSettings').classList.add('active');
    
    // 显示/隐藏相应面板
    var subscribePanel = document.getElementById('subscribeTopicsPanel');
    subscribePanel.style.display = 'none';
    var publishPanel = document.getElementById('publishPanel');
    publishPanel.style.display = 'none';
    var settingsPanel = document.getElementById('settingsPanel');
    settingsPanel.style.display = 'block';
    
    // 获取MQTT设置和状态
    getMqttSettings();
    checkMqttStatus();
}

// 获取MQTT状态
function checkMqttStatus() {
    Ajax.get(CONSTANT.GET_MQTT_STATUS_URL, function(res) {
        try {
            res = JSON.parse(res);
            console.log('获取MQTT状态：', res);
            
            if (res.success) {
                updateMqttStatusUI(res.status, res.status_text, res.error_message);
                currentMqttStatus = res.status;
            }
        } catch (e) {
            console.error('解析MQTT状态数据失败', e);
            document.getElementById('mqttStatusDisplay').innerHTML = 
                '<span style="color: red;">获取状态失败</span>';
        }
    });
}

// 更新MQTT状态UI
function updateMqttStatusUI(status, statusText, errorMessage) {
    const statusDiv = document.getElementById('mqttStatusDisplay');
    if (!statusDiv) return;
    
    let statusHtml = '';
    let statusColor = '';
    
    switch(status) {
        case 0: // 断开连接
            statusColor = '#999999';
            statusHtml = `<span style="color: ${statusColor};">⚪ ${statusText}</span>`;
            break;
        case 1: // 连接中
            statusColor = '#FFA500';
            statusHtml = `<span style="color: ${statusColor};">🟡 ${statusText}</span>`;
            break;
        case 2: // 已连接
            statusColor = '#28a745';
            statusHtml = `<span style="color: ${statusColor};">🟢 ${statusText}</span>`;
            break;
        default: // 错误状态
            statusColor = '#dc3545';
            statusHtml = `<span style="color: ${statusColor};">🔴 ${statusText}</span>`;
            if (errorMessage && errorMessage.length > 0) {
                statusHtml += `<div style="margin-top: 5px; font-size: 0.9em; color: #dc3545;">${errorMessage}</div>`;
            }
    }
    
    statusDiv.innerHTML = statusHtml;
}

// 开始定期检查MQTT状态
function startMqttStatusCheck() {
    if (mqttStatusCheckTimer) {
        clearInterval(mqttStatusCheckTimer);
    }
    
    // 立即检查一次
    checkMqttStatus();
    
    // 每5秒检查一次状态
    mqttStatusCheckTimer = setInterval(checkMqttStatus, 5000);
}

// 停止定期检查MQTT状态
function stopMqttStatusCheck() {
    if (mqttStatusCheckTimer) {
        clearInterval(mqttStatusCheckTimer);
        mqttStatusCheckTimer = null;
    }
}

// 获取MQTT设置
function getMqttSettings() {
    Ajax.get(CONSTANT.GET_MQTT_SETTINGS_URL, function(res) {
        try {
            res = JSON.parse(res);
            console.log('获取MQTT设置：', res);
            
            if (res.success) {
                // 填充表单
                document.getElementById('mqttBroker').value = res.broker || '';
                document.getElementById('mqttUsername').value = res.username || '';
                document.getElementById('mqttPassword').value = res.password || '';
                
                // 清除错误信息
                showErrorMessage('settingsError', '');
                document.getElementById('settingsSuccess').textContent = '';
            } else {
                showErrorMessage('settingsError', '获取MQTT设置失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析MQTT设置数据失败', e);
            showErrorMessage('settingsError', '解析设置数据失败');
        }
    });
}

// 保存MQTT设置
function saveMqttSettings() {
    var broker = document.getElementById('mqttBroker').value.trim();
    var username = document.getElementById('mqttUsername').value.trim();
    var password = document.getElementById('mqttPassword').value;
    
    if (!broker) {
        showErrorMessage('settingsError', 'MQTT Broker地址不能为空');
        return;
    }
    
    // 检查broker格式是否合法
    if (!broker.startsWith('mqtt://') && !broker.startsWith('mqtts://')) {
        showErrorMessage('settingsError', 'Broker地址必须以mqtt://或mqtts://开头');
        return;
    }
    
    // 禁用按钮
    var saveButton = document.querySelector('#settingsPanel .btn-primary');
    saveButton.disabled = true;
    
    // 清除成功消息
    document.getElementById('settingsSuccess').textContent = '';
    
    Ajax.post(CONSTANT.SAVE_MQTT_SETTINGS_URL, { 
        broker: broker, 
        username: username, 
        password: password 
    }, function(res) {
        try {
            res = JSON.parse(res);
            console.log('保存MQTT设置结果：', res);
            
            if (res.success) {
                document.getElementById('settingsSuccess').textContent = 'MQTT设置保存成功！';
                showErrorMessage('settingsError', '');
                
                // 立即检查MQTT状态以更新UI
                setTimeout(checkMqttStatus, 1000);
                
                // 5秒后清除成功消息
                setTimeout(function() {
                    document.getElementById('settingsSuccess').textContent = '';
                }, 5000);
            } else {
                showErrorMessage('settingsError', '保存MQTT设置失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析保存MQTT设置结果失败', e);
            showErrorMessage('settingsError', '保存MQTT设置失败，请重试');
        } finally {
            // 恢复按钮状态
            saveButton.disabled = false;
        }
    });
}

// 切换密码可见性
function togglePasswordVisibility() {
    var passwordInput = document.getElementById('mqttPassword');
    if (passwordInput.type === 'password') {
        passwordInput.type = 'text';
    } else {
        passwordInput.type = 'password';
    }
}

// 获取主题列表
function getTopicsList() {
    // 显示加载状态
    var topicsList = document.getElementById('topicsList');
    topicsList.innerHTML = '<tr><td colspan="2" style="text-align: center;">正在加载...</td></tr>';
    
    Ajax.get(CONSTANT.GET_TOPICS_URL, function(res) {
        try {
            res = JSON.parse(res);
            console.log('获取主题列表：', res);
            
            if (res.success) {
                renderTopicsList(res.topics);
            } else {
                showErrorMessage('topicError', '获取主题列表失败: ' + res.message);
                topicsList.innerHTML = '<tr><td colspan="2" style="text-align: center; color: red;">获取主题列表失败</td></tr>';
            }
        } catch (e) {
            console.error('解析主题列表数据失败', e);
            showErrorMessage('topicError', '解析主题列表数据失败');
            topicsList.innerHTML = '<tr><td colspan="2" style="text-align: center; color: red;">解析数据失败</td></tr>';
        }
    });
}

// 渲染主题列表
function renderTopicsList(topics) {
    var topicsList = document.getElementById('topicsList');
    topicsList.innerHTML = '';
    
    if (!topics || topics.length === 0) {
        var noDataRow = document.createElement('tr');
        noDataRow.innerHTML = '<td colspan="2" style="text-align: center;">暂无订阅主题</td>';
        topicsList.appendChild(noDataRow);
        return;
    }
    
    // 检测是否为移动设备
    var isMobile = window.innerWidth <= 768;
    
    topics.forEach(function(topic) {
        var row = document.createElement('tr');
        // 如果主题名称过长，在移动设备上进行裁剪显示
        var displayTopic = isMobile && topic.length > 20 ? 
            topic.substring(0, 17) + '...' : 
            topic;
            
        row.innerHTML = `
            <td title="${topic}">${displayTopic}</td>
            <td>
                <button class="btn-danger" onclick="deleteTopic('${topic}')">删除</button>
            </td>
        `;
        topicsList.appendChild(row);
    });
}

// 添加主题
function addTopic() {
    var newTopic = document.getElementById('newTopic').value.trim();
    if (!newTopic) {
        showErrorMessage('topicError', '主题名称不能为空');
        return;
    }
    
    // 禁用按钮
    var addButton = document.querySelector('.btn-primary');
    addButton.disabled = true;
    
    Ajax.post(CONSTANT.ADD_TOPIC_URL, { topic: newTopic }, function(res) {
        try {
            res = JSON.parse(res);
            console.log('添加主题结果：', res);
            
            if (res.success) {
                document.getElementById('newTopic').value = '';
                showErrorMessage('topicError', '添加成功！', false);
                getTopicsList();
                
                // 3秒后清除成功消息
                setTimeout(function() {
                    showErrorMessage('topicError', '');
                }, 3000);
            } else {
                showErrorMessage('topicError', '添加主题失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析添加主题结果失败', e);
            showErrorMessage('topicError', '添加主题失败，请重试');
        } finally {
            // 恢复按钮状态
            addButton.disabled = false;
        }
    });
}

// 删除主题
function deleteTopic(topic) {
    if (confirm('确定要删除主题 "' + topic + '" 吗？')) {
        Ajax.post(CONSTANT.DELETE_TOPIC_URL, { topic: topic }, function(res) {
            try {
                res = JSON.parse(res);
                console.log('删除主题结果：', res);
                
                if (res.success) {
                    showErrorMessage('topicError', '删除成功！', false);
                    getTopicsList();
                    
                    // 3秒后清除成功消息
                    setTimeout(function() {
                        showErrorMessage('topicError', '');
                    }, 3000);
                } else {
                    showErrorMessage('topicError', '删除主题失败: ' + res.message);
                }
            } catch (e) {
                console.error('解析删除主题结果失败', e);
                showErrorMessage('topicError', '删除主题失败，请重试');
            }
        });
    }
}

// 发布消息
function publishMessage() {
    var topic = document.getElementById('publishTopic').value.trim();
    var message = document.getElementById('publishMessage').value.trim();
    
    if (!topic) {
        showErrorMessage('publishError', '主题名称不能为空');
        return;
    }
    
    if (!message) {
        showErrorMessage('publishError', '消息内容不能为空');
        return;
    }
    
    // 禁用按钮
    var publishButton = document.querySelector('#publishPanel .btn-primary');
    publishButton.disabled = true;
    
    Ajax.post(CONSTANT.PUBLISH_MESSAGE_URL, { topic: topic, message: message }, function(res) {
        try {
            res = JSON.parse(res);
            console.log('发布消息结果：', res);
            
            if (res.success) {
                showErrorMessage('publishError', '消息发布成功!', false);
                setTimeout(function() {
                    showErrorMessage('publishError', '');
                }, 3000);
            } else {
                showErrorMessage('publishError', '发布消息失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析发布消息结果失败', e);
            showErrorMessage('publishError', '发布消息失败，请重试');
        } finally {
            // 恢复按钮状态
            publishButton.disabled = false;
        }
    });
}

// 显示错误或成功信息
function showErrorMessage(elementId, message, isError = true) {
    var element = document.getElementById(elementId);
    element.innerHTML = message;
    element.style.color = isError ? 'red' : 'green';
}

// 菜单点击事件
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

// 窗口大小改变时重新渲染主题列表
window.addEventListener('resize', function() {
    if (document.getElementById('subscribeTopicsPanel').style.display === 'block') {
        getTopicsList();
    }
});

// 添加键盘事件支持
function setupKeyboardSupport() {
    // 为主题输入框添加回车键支持
    document.getElementById('newTopic').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            addTopic();
        }
    });
}

// 页面初始化
function initPage() {
    // 初始化页面逻辑
    showSubscribeTopics();
    
    // 设置键盘支持
    setupKeyboardSupport();
    
    // 开始定期检查MQTT状态
    startMqttStatusCheck();
}

// 页面卸载时清理
window.addEventListener('beforeunload', function() {
    stopMqttStatusCheck();
});

// 页面加载完成后执行初始化
window.onload = initPage; 