// 基础URL
const baseurl = window.location.origin;
console.log(baseurl);

// API常量
const CONSTANT = {
    GET_TOPICS_URL: `${baseurl}/api/mqtt/topics`,
    ADD_TOPIC_URL: `${baseurl}/api/mqtt/topics/add`,
    DELETE_TOPIC_URL: `${baseurl}/api/mqtt/topics/delete`,
    PUBLISH_MESSAGE_URL: `${baseurl}/api/mqtt/publish`,
};

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
    var base1 = document.querySelector('.header-title-one');
    base1.style.color = '#000';
    base1.style.cursor = 'auto';
    var base2 = document.querySelector('.header-title-two');
    base2.style.color = '#888888';
    base2.style.cursor = 'pointer';
    
    var subscribePanel = document.getElementById('subscribeTopicsPanel');
    subscribePanel.style.display = 'block';
    var publishPanel = document.getElementById('publishPanel');
    publishPanel.style.display = 'none';
    
    // 获取主题列表
    getTopicsList();
}

// 显示发布消息面板
function showPublishPanel() {
    console.log('显示发布消息面板');
    var base1 = document.querySelector('.header-title-one');
    base1.style.color = '#888888';
    base1.style.cursor = 'pointer';
    var base2 = document.querySelector('.header-title-two');
    base2.style.color = '#000';
    base2.style.cursor = 'auto';
    
    var subscribePanel = document.getElementById('subscribeTopicsPanel');
    subscribePanel.style.display = 'none';
    var publishPanel = document.getElementById('publishPanel');
    publishPanel.style.display = 'block';
}

// 获取主题列表
function getTopicsList() {
    Ajax.get(CONSTANT.GET_TOPICS_URL, function(res) {
        try {
            res = JSON.parse(res);
            console.log('获取主题列表：', res);
            
            if (res.success) {
                renderTopicsList(res.topics);
            } else {
                showErrorMessage('topicError', '获取主题列表失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析主题列表数据失败', e);
            showErrorMessage('topicError', '解析主题列表数据失败');
        }
    });
}

// 渲染主题列表
function renderTopicsList(topics) {
    var topicsList = document.getElementById('topicsList');
    topicsList.innerHTML = '';
    
    if (!topics || topics.length === 0) {
        var noDataRow = document.createElement('tr');
        noDataRow.innerHTML = '<td colspan="3">暂无订阅主题</td>';
        topicsList.appendChild(noDataRow);
        return;
    }
    
    topics.forEach(function(topic, index) {
        var row = document.createElement('tr');
        row.innerHTML = `
            <td>${index + 1}</td>
            <td>${topic}</td>
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
    
    Ajax.post(CONSTANT.ADD_TOPIC_URL, { topic: newTopic }, function(res) {
        try {
            res = JSON.parse(res);
            console.log('添加主题结果：', res);
            
            if (res.success) {
                document.getElementById('newTopic').value = '';
                showErrorMessage('topicError', '');
                getTopicsList();
            } else {
                showErrorMessage('topicError', '添加主题失败: ' + res.message);
            }
        } catch (e) {
            console.error('解析添加主题结果失败', e);
            showErrorMessage('topicError', '添加主题失败，请重试');
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
                    getTopicsList();
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
        }
    });
}

// 显示错误信息
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

// 页面初始化
function initPage() {
    console.log('MQTT主题管理页面初始化');
    showSubscribeTopics();
}

// 页面加载完成时执行初始化
window.onload = function() {
    initPage();
}; 