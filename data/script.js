// 全局变量
let updateInterval = 500; // 默认更新间隔为500ms
let maxDataPoints = 50;  // 默认最大数据点数
let flowData = {
    timestamps: [],
    flowX: [],
    flowY: [],
    height: []
};

// 图表配置
let flowChart, heightChart;

// 当页面加载完成时
document.addEventListener('DOMContentLoaded', function() {
    // 从本地存储中获取设置
    const savedInterval = localStorage.getItem('updateInterval');
    const savedPoints = localStorage.getItem('chartPoints');
    
    if (savedInterval) {
        updateInterval = parseInt(savedInterval);
        document.getElementById('update-interval').value = updateInterval;
    }
    
    if (savedPoints) {
        maxDataPoints = parseInt(savedPoints);
        document.getElementById('chart-points').value = maxDataPoints;
    }
    
    // 初始化图表
    initCharts();
    
    // 绘制初始光流向量
    drawFlowVector(0, 0);
    
    // 开始定期更新数据
    fetchData();
    setInterval(fetchData, updateInterval);
    
    // 设置按钮事件
    document.getElementById('save-settings').addEventListener('click', saveSettings);
});

// 初始化图表
function initCharts() {
    // 光流图表
    const flowCtx = document.getElementById('flow-chart').getContext('2d');
    flowChart = new Chart(flowCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'X方向光流',
                    data: [],
                    borderColor: 'rgba(255, 99, 132, 1)',
                    backgroundColor: 'rgba(255, 99, 132, 0.2)',
                    borderWidth: 2,
                    tension: 0.3,
                    fill: false
                },
                {
                    label: 'Y方向光流',
                    data: [],
                    borderColor: 'rgba(54, 162, 235, 1)',
                    backgroundColor: 'rgba(54, 162, 235, 0.2)',
                    borderWidth: 2,
                    tension: 0.3,
                    fill: false
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: '时间'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: '光流值'
                    }
                }
            }
        }
    });
    
    // 高度图表
    const heightCtx = document.getElementById('height-chart').getContext('2d');
    heightChart = new Chart(heightCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: '高度 (mm)',
                    data: [],
                    borderColor: 'rgba(75, 192, 192, 1)',
                    backgroundColor: 'rgba(75, 192, 192, 0.2)',
                    borderWidth: 2,
                    tension: 0.3,
                    fill: true
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: '时间'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: '高度 (mm)'
                    }
                }
            }
        }
    });
}

// 保存设置
function saveSettings() {
    const newInterval = parseInt(document.getElementById('update-interval').value);
    const newPoints = parseInt(document.getElementById('chart-points').value);
    
    // 验证输入
    if (isNaN(newInterval) || newInterval < 100 || newInterval > 5000) {
        alert('请输入有效的更新间隔 (100-5000 ms)');
        return;
    }
    
    if (isNaN(newPoints) || newPoints < 10 || newPoints > 200) {
        alert('请输入有效的图表点数 (10-200)');
        return;
    }
    
    // 更新值
    updateInterval = newInterval;
    maxDataPoints = newPoints;
    
    // 保存到本地存储
    localStorage.setItem('updateInterval', updateInterval);
    localStorage.setItem('chartPoints', maxDataPoints);
    
    // 重新初始化定时器
    clearInterval(window.dataInterval);
    window.dataInterval = setInterval(fetchData, updateInterval);
    
    alert('设置已保存');
}

// 获取数据
async function fetchData() {
    try {
        const response = await fetch('/data');
        if (!response.ok) {
            throw new Error(`HTTP error! Status: ${response.status}`);
        }
        
        const data = await response.json();
        updateUI(data);
    } catch (error) {
        console.error('获取数据失败:', error);
    }
}

// 更新UI
function updateUI(data) {
    // 更新实时数据显示
    document.getElementById('flow-x').textContent = data.flow_x;
    document.getElementById('flow-y').textContent = data.flow_y;
    document.getElementById('height').textContent = data.height.toFixed(2);
    document.getElementById('quality').textContent = data.quality;
    
    // 格式化时间戳
    const date = new Date();
    const timeString = date.toLocaleTimeString();
    document.getElementById('timestamp').textContent = timeString;
    
    // 更新光流向量可视化
    drawFlowVector(data.flow_x, data.flow_y);
    
    // 更新图表数据
    updateChartData(timeString, data);
}

// 更新图表数据
function updateChartData(timeString, data) {
    // 添加新数据点
    flowData.timestamps.push(timeString);
    flowData.flowX.push(data.flow_x);
    flowData.flowY.push(data.flow_y);
    flowData.height.push(data.height);
    
    // 限制数据点数量
    if (flowData.timestamps.length > maxDataPoints) {
        flowData.timestamps.shift();
        flowData.flowX.shift();
        flowData.flowY.shift();
        flowData.height.shift();
    }
    
    // 更新图表
    flowChart.data.labels = flowData.timestamps;
    flowChart.data.datasets[0].data = flowData.flowX;
    flowChart.data.datasets[1].data = flowData.flowY;
    flowChart.update();
    
    heightChart.data.labels = flowData.timestamps;
    heightChart.data.datasets[0].data = flowData.height;
    heightChart.update();
}

// 绘制光流向量
function drawFlowVector(x, y) {
    const canvas = document.getElementById('flow-vector');
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;
    const centerX = width / 2;
    const centerY = height / 2;
    
    // 缩放因子，根据实际数据范围调整
    const scaleFactor = 0.5;
    
    // 清除画布
    ctx.clearRect(0, 0, width, height);
    
    // 画背景圆
    ctx.beginPath();
    ctx.arc(centerX, centerY, Math.min(width, height) * 0.4, 0, 2 * Math.PI);
    ctx.fillStyle = 'rgba(200, 200, 200, 0.2)';
    ctx.fill();
    ctx.strokeStyle = 'rgba(150, 150, 150, 0.5)';
    ctx.stroke();
    
    // 画中心点
    ctx.beginPath();
    ctx.arc(centerX, centerY, 5, 0, 2 * Math.PI);
    ctx.fillStyle = 'rgba(50, 50, 50, 0.8)';
    ctx.fill();
    
    // 计算向量终点
    const endX = centerX + x * scaleFactor;
    const endY = centerY + y * scaleFactor;
    
    // 画向量线
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(endX, endY);
    ctx.strokeStyle = 'rgba(255, 0, 0, 0.8)';
    ctx.lineWidth = 3;
    ctx.stroke();
    
    // 画箭头
    const angle = Math.atan2(endY - centerY, endX - centerX);
    const arrowSize = 10;
    
    ctx.beginPath();
    ctx.moveTo(endX, endY);
    ctx.lineTo(
        endX - arrowSize * Math.cos(angle - Math.PI / 6),
        endY - arrowSize * Math.sin(angle - Math.PI / 6)
    );
    ctx.lineTo(
        endX - arrowSize * Math.cos(angle + Math.PI / 6),
        endY - arrowSize * Math.sin(angle + Math.PI / 6)
    );
    ctx.closePath();
    ctx.fillStyle = 'rgba(255, 0, 0, 0.8)';
    ctx.fill();
    
    // 显示向量数值
    ctx.font = '12px Arial';
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.fillText(`(${x}, ${y})`, centerX, height - 10);
} 