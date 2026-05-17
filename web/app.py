"""
超市管理系统 Web 版 - Flask 应用
"""
import os
import sqlite3
import hashlib
import time
from datetime import datetime, timedelta
from flask import Flask, render_template, request, session, redirect, url_for, jsonify, send_file
from contextlib import contextmanager

app = Flask(__name__)
app.secret_key = 'supermarket_secret_key_2025'
app.config['DATABASE'] = 'instance/supermarket.db'

# 确保数据目录存在
os.makedirs('instance', exist_ok=True)

# ==================== 数据库初始化 ====================

def get_db():
    """获取数据库连接"""
    db = sqlite3.connect(app.config['DATABASE'])
    db.row_factory = sqlite3.Row
    return db

@contextmanager
def get_db_cursor():
    """上下文管理器，自动关闭数据库连接"""
    db = get_db()
    try:
        yield db.cursor()
        db.commit()
    except Exception as e:
        db.rollback()
        raise e
    finally:
        db.close()

def init_db():
    """初始化数据库表"""
    db = get_db()
    cursor = db.cursor()
    
    # 员工表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS employees (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            role TEXT DEFAULT 'cashier',
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 商品表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS products (
            id TEXT PRIMARY KEY,
            barcode TEXT UNIQUE,
            name TEXT NOT NULL,
            price REAL NOT NULL,
            cost REAL DEFAULT 0,
            stock INTEGER DEFAULT 0,
            category TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 会员表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS members (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            phone TEXT UNIQUE NOT NULL,
            level INTEGER DEFAULT 0,
            points INTEGER DEFAULT 0,
            total_consume REAL DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            last_consume_at INTEGER
        )
    ''')
    
    # 储值卡表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS vip_cards (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            card_no TEXT UNIQUE NOT NULL,
            member_id INTEGER,
            balance REAL DEFAULT 0,
            password_hash TEXT,
            salt TEXT,
            status INTEGER DEFAULT 1,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            FOREIGN KEY (member_id) REFERENCES members(id)
        )
    ''')
    
    # 储值卡交易表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS vip_card_tx (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            card_no TEXT NOT NULL,
            type INTEGER,
            amount REAL,
            balance_before REAL,
            operator_id INTEGER,
            sale_id INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 销售表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sales (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            cashier_id INTEGER,
            member_id INTEGER,
            total_amount REAL,
            discount REAL DEFAULT 0,
            final_amount REAL,
            payment_method TEXT,
            points_used INTEGER DEFAULT 0,
            status INTEGER DEFAULT 0,
            promotion_id INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            completed_at INTEGER
        )
    ''')
    
    # 确保 promotion_id 字段存在（兼容已有数据库）
    cursor.execute("PRAGMA table_info(sales)")
    columns = [col[1] for col in cursor.fetchall()]
    if 'promotion_id' not in columns:
        cursor.execute("ALTER TABLE sales ADD COLUMN promotion_id INTEGER")
    
    # 退款记录表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS refunds (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sale_id INTEGER,
            reason TEXT,
            refund_amount REAL,
            operator_id INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 销售明细表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sale_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sale_id INTEGER,
            product_id TEXT,
            product_name TEXT,
            price REAL,
            quantity INTEGER,
            subtotal REAL,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 促销表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS promotions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            type INTEGER,
            value REAL,
            start_time INTEGER,
            end_time INTEGER,
            status INTEGER DEFAULT 1,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    ''')
    
    # 采购表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS purchases (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            supplier TEXT,
            total_amount REAL,
            status INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            completed_at INTEGER
        )
    ''')
    
    # 创建默认管理员账号
    cursor.execute("SELECT * FROM employees WHERE name = 'admin'")
    if not cursor.fetchone():
        salt = generate_salt()
        pwd_hash = hash_password('admin123', salt)
        cursor.execute(
            "INSERT INTO employees (name, password_hash, salt, role) VALUES (?, ?, ?, ?)",
            ('admin', pwd_hash, salt, 'admin')
        )
    
    # 创建示例商品
    cursor.execute("SELECT COUNT(*) FROM products")
    if cursor.fetchone()[0] == 0:
        sample_products = [
            ('P001', '6901234567890', '农夫山泉', 2.00, 1.00, 100, '饮料'),
            ('P002', '6901234567891', '可口可乐', 3.00, 1.50, 80, '饮料'),
            ('P003', '6901234567892', '康师傅方便面', 4.50, 2.00, 120, '食品'),
            ('P004', '6901234567893', '奥利奥饼干', 8.00, 4.00, 60, '食品'),
            ('P005', '6901234567894', '飘柔洗发水', 35.00, 18.00, 30, '日用品'),
        ]
        cursor.executemany(
            "INSERT INTO products (id, barcode, name, price, cost, stock, category) VALUES (?, ?, ?, ?, ?, ?, ?)",
            sample_products
        )
    
    db.commit()
    db.close()

# ==================== 工具函数 ====================

def generate_salt():
    """生成盐值"""
    return hashlib.md5(str(time.time()).encode()).hexdigest()[:16]

def hash_password(password, salt):
    """哈希密码"""
    return hashlib.sha256((password + salt).encode()).hexdigest()

def get_timestamp():
    """获取当前时间戳"""
    return int(time.time())

def format_time(timestamp):
    """格式化时间戳"""
    if not timestamp:
        return ''
    return datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M')

# ==================== 积分计算 ====================

def calculate_ladder_points(amount):
    """计算阶梯积分"""
    points = 0
    if amount >= 1000:
        points = int(amount * 5)
    elif amount >= 500:
        points = int(100 * 2 + (amount - 100) * 3)
    elif amount >= 100:
        points = int(100 * 1 + (amount - 100) * 2)
    else:
        points = int(amount * 1)
    return points

def get_vip_discount(level):
    """获取储值卡折扣率"""
    discounts = {0: 0.00, 1: 0.02, 2: 0.05, 3: 0.10}
    return discounts.get(level, 0.00)

def get_member_level_name(level):
    """获取会员等级名称"""
    names = {0: '普通会员', 1: '银卡会员', 2: '金卡会员', 3: '钻石会员'}
    return names.get(level, '普通会员')

def upgrade_member_level(points, total_consume):
    """根据消费升级会员等级"""
    if total_consume >= 10000:
        return 3  # 钻石会员
    elif total_consume >= 5000:
        return 2  # 金卡会员
    elif total_consume >= 1000:
        return 1  # 银卡会员
    return 0  # 普通会员

def get_active_promotions():
    """获取当前有效的促销活动"""
    now = get_timestamp()
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT * FROM promotions 
            WHERE status = 1 AND start_time <= ? AND end_time >= ?
            ORDER BY id DESC
        ''', (now, now))
        return [dict(row) for row in cursor.fetchall()]

def calculate_promotion_discount(total_amount, promos):
    """计算促销优惠金额"""
    total_discount = 0
    promo_names = []
    
    for promo in promos:
        if promo['type'] == 0:  # 满减
            if total_amount >= promo['value']:
                discount = promo.get('extra_value', 0)
                if discount > 0:
                    total_discount += discount
                    promo_names.append(f"满{int(promo['value'])}减{int(discount)}")
        elif promo['type'] == 1:  # 折扣
            if total_amount >= promo['value']:
                discount = total_amount * (1 - promo['extra_value'] / 100)
                if discount > 0:
                    total_discount += discount
                    promo_names.append(f"满{int(promo['value'])}元{int(promo['extra_value'])}%折")
    
    return total_discount, promo_names

def get_active_promotion():
    """获取当前有效的促销活动"""
    now = get_timestamp()
    db = get_db()
    cursor = db.cursor()
    cursor.execute(
        "SELECT * FROM promotions WHERE status = 1 AND start_time <= ? AND end_time >= ? LIMIT 1",
        (now, now)
    )
    promo = cursor.fetchone()
    db.close()
    return dict(promo) if promo else None

def calculate_promotion_discount(promo, total_amount):
    """计算促销优惠金额
    promo_type: 0=满减（如满100减10），1=折扣（如打8折）
    promo_value: 满减时为减免金额，折扣时为折扣率（如0.8表示8折）
    """
    if not promo:
        return 0, None
    
    promo_type = promo.get('type', 0)
    promo_value = promo.get('value', 0)
    
    # type=0: 满减促销，检查是否满足条件
    if promo_type == 0:
        # 满 promo_value 元减 value 元（实际满减值存储在 value 字段）
        threshold = promo_value * 10  # 假设满100减10时，type=0, value=10
        if total_amount >= threshold:
            return promo_value, promo['id']
        return 0, None
    
    # type=1: 折扣促销
    elif promo_type == 1:
        discount_amount = total_amount * (1 - promo_value)
        return discount_amount, promo['id']
    
    return 0, None

# ==================== 路由 ====================

@app.route('/')
def index():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('index.html', 
                           username=session.get('username'),
                           role=session.get('role'))

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        name = request.form.get('username', '')
        pwd = request.form.get('password', '')
        
        with get_db_cursor() as cursor:
            cursor.execute(
                "SELECT * FROM employees WHERE name = ?",
                (name,)
            )
            user = cursor.fetchone()
            
            if user and user['password_hash'] == hash_password(pwd, user['salt']):
                session['user_id'] = user['id']
                session['username'] = user['name']
                session['role'] = user['role']
                return redirect(url_for('index'))
            
        return render_template('login.html', error='用户名或密码错误')
    
    return render_template('login.html')

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

# ==================== 销售收银 ====================

@app.route('/sale')
def sale():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('sale.html', username=session.get('username'))

@app.route('/api/products')
def api_products():
    """获取商品列表"""
    barcode = request.args.get('barcode', '')
    with get_db_cursor() as cursor:
        if barcode:
            cursor.execute(
                "SELECT * FROM products WHERE barcode = ? OR name LIKE ?",
                (barcode, f'%{barcode}%')
            )
        else:
            cursor.execute("SELECT * FROM products LIMIT 100")
        products = [dict(row) for row in cursor.fetchall()]
    return jsonify(products)

@app.route('/api/product/search')
def api_product_search():
    """搜索商品"""
    keyword = request.args.get('keyword', '')
    with get_db_cursor() as cursor:
        cursor.execute(
            "SELECT * FROM products WHERE barcode LIKE ? OR name LIKE ? OR id LIKE ? LIMIT 20",
            (f'%{keyword}%', f'%{keyword}%', f'%{keyword}%')
        )
        products = [dict(row) for row in cursor.fetchall()]
    return jsonify(products)

@app.route('/api/member/search')
def api_member_search():
    """搜索会员"""
    keyword = request.args.get('keyword', '')
    with get_db_cursor() as cursor:
        cursor.execute(
            "SELECT * FROM members WHERE phone LIKE ? OR name LIKE ? LIMIT 10",
            (f'%{keyword}%', f'%{keyword}%')
        )
        members = [dict(row) for row in cursor.fetchall()]
    return jsonify(members)

@app.route('/api/sale/create', methods=['POST'])
def api_sale_create():
    """创建销售"""
    data = request.json
    items = data.get('items', [])
    member_id = data.get('member_id')
    payment_method = data.get('payment_method', '现金')
    
    if not items:
        return jsonify({'error': '购物车为空'}), 400
    
    # 计算金额
    total_amount = sum(item['price'] * item['quantity'] for item in items)
    discount = 0
    promotion_id = None
    final_amount = total_amount - discount
    
    # 查询当前有效促销活动并计算优惠
    promo = get_active_promotion()
    if promo:
        promo_discount, promo_id = calculate_promotion_discount(promo, total_amount)
        if promo_discount > 0:
            discount = promo_discount
            promotion_id = promo_id
            final_amount = total_amount - discount
    
    # 如果使用储值卡支付
    if payment_method == '储值卡' and member_id:
        vip_card_password = data.get('vip_card_password', '')
        
        with get_db_cursor() as cursor:
            cursor.execute("SELECT * FROM vip_cards WHERE member_id = ? AND status = 1", (member_id,))
            vip_card = cursor.fetchone()
            if vip_card:
                # 验证储值卡密码
                if vip_card['password_hash'] and vip_card['salt']:
                    stored_hash = vip_card['password_hash']
                    input_hash = hash_password(vip_card_password, vip_card['salt'])
                    if input_hash != stored_hash:
                        return jsonify({'error': '储值卡密码错误'}), 400
                
                discount_rate = get_vip_discount(data.get('member_level', 0))
                # 储值卡折扣在促销之后计算
                vip_discount = final_amount * discount_rate
                final_amount = final_amount - vip_discount
                
                # 扣除储值卡余额
                if vip_card['balance'] >= final_amount:
                    cursor.execute(
                        "UPDATE vip_cards SET balance = balance - ? WHERE id = ?",
                        (final_amount, vip_card['id'])
                    )
                    cursor.execute(
                        "INSERT INTO vip_card_tx (card_no, type, amount, balance_before, operator_id, sale_id) VALUES (?, 1, ?, ?, ?, ?)",
                        (vip_card['card_no'], final_amount, vip_card['balance'], session['user_id'], sale_id if 'sale_id' in dir() else None)
                    )
                else:
                    return jsonify({'error': '储值卡余额不足'}), 400
            else:
                return jsonify({'error': '会员没有储值卡'}), 400
    
    with get_db_cursor() as cursor:
        # 创建销售记录，包含促销来源
        cursor.execute('''
            INSERT INTO sales (cashier_id, member_id, total_amount, discount, final_amount, payment_method, status, promotion_id, created_at, completed_at)
            VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?, ?)
        ''', (session['user_id'], member_id, total_amount, discount, final_amount, payment_method, promotion_id, get_timestamp(), get_timestamp()))
        sale_id = cursor.lastrowid
        
        # 添加销售明细
        for item in items:
            cursor.execute('''
                INSERT INTO sale_items (sale_id, product_id, product_name, price, quantity, subtotal)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (sale_id, item['id'], item['name'], item['price'], item['quantity'], item['price'] * item['quantity']))
            
            # 扣减库存
            cursor.execute(
                "UPDATE products SET stock = stock - ? WHERE id = ?",
                (item['quantity'], item['id'])
            )
        
        # 更新会员积分
        if member_id:
            cursor.execute("SELECT * FROM members WHERE id = ?", (member_id,))
            member = cursor.fetchone()
            if member:
                earned_points = calculate_ladder_points(final_amount)
                new_points = member['points'] + earned_points
                new_consume = member['total_consume'] + final_amount
                new_level = upgrade_member_level(new_points, new_consume)
                
                cursor.execute('''
                    UPDATE members SET points = ?, total_consume = ?, level = ?, last_consume_at = ?
                    WHERE id = ?
                ''', (new_points, new_consume, new_level, get_timestamp(), member_id))
    
    return jsonify({'success': True, 'sale_id': sale_id, 'final_amount': final_amount, 'discount': discount, 'promotion_id': promotion_id})

@app.route('/api/sales/today')
def api_sales_today():
    """获取今日销售"""
    today_start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT * FROM sales WHERE created_at >= ? AND status = 1
            ORDER BY created_at DESC
        ''', (today_start,))
        sales = [dict(row) for row in cursor.fetchall()]
    
    total = sum(s['final_amount'] for s in sales)
    return jsonify({'sales': sales, 'total': total, 'count': len(sales)})

@app.route('/api/sales/refundable')
def api_sales_refundable():
    """获取可退款的订单列表 - 只返回今日内status=1的订单"""
    today_start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT s.*, e.name as cashier_name 
            FROM sales s 
            LEFT JOIN employees e ON s.cashier_id = e.id
            WHERE s.created_at >= ? AND s.status = 1
            ORDER BY s.created_at DESC
        ''', (today_start,))
        sales = [dict(row) for row in cursor.fetchall()]
    
    return jsonify({'sales': sales, 'count': len(sales)})

@app.route('/api/sale/refund', methods=['POST'])
def api_sale_refund():
    """退款API"""
    data = request.json
    sale_id = data.get('sale_id')
    reason = data.get('reason', '')
    
    if not sale_id:
        return jsonify({'error': '缺少订单ID'}), 400
    
    with get_db_cursor() as cursor:
        # 检查订单是否存在且状态为已完成(status=1)
        cursor.execute("SELECT * FROM sales WHERE id = ?", (sale_id,))
        sale = cursor.fetchone()
        
        if not sale:
            return jsonify({'error': '订单不存在'}), 404
        
        if sale['status'] != 1:
            return jsonify({'error': '订单状态不允许退款'}), 400
        
        sale = dict(sale)
        
        # 将订单状态改为已退款(status=2)
        cursor.execute("UPDATE sales SET status = 2 WHERE id = ?", (sale_id,))
        
        # 恢复商品库存
        cursor.execute("SELECT * FROM sale_items WHERE sale_id = ?", (sale_id,))
        items = cursor.fetchall()
        for item in items:
            cursor.execute(
                "UPDATE products SET stock = stock + ? WHERE id = ?",
                (item['quantity'], item['product_id'])
            )
        
        # 如果原订单使用储值卡，退还余额
        if sale['payment_method'] == '储值卡' and sale['member_id']:
            cursor.execute("SELECT * FROM vip_cards WHERE member_id = ? AND status = 1", (sale['member_id'],))
            vip_card = cursor.fetchone()
            if vip_card:
                cursor.execute(
                    "UPDATE vip_cards SET balance = balance + ? WHERE id = ?",
                    (sale['final_amount'], vip_card['id'])
                )
                cursor.execute(
                    "INSERT INTO vip_card_tx (card_no, type, amount, balance_before, operator_id, sale_id) VALUES (?, 2, ?, ?, ?, ?)",
                    (vip_card['card_no'], sale['final_amount'], vip_card['balance'], session['user_id'], sale_id)
                )
        
        # 如果原订单有会员，更新会员积分(扣除)
        if sale['member_id']:
            cursor.execute("SELECT * FROM members WHERE id = ?", (sale['member_id'],))
            member = cursor.fetchone()
            if member:
                # 计算本次消费扣除的积分
                refund_points = calculate_ladder_points(sale['final_amount'])
                new_points = max(0, member['points'] - refund_points)
                new_consume = max(0, member['total_consume'] - sale['final_amount'])
                new_level = upgrade_member_level(new_points, new_consume)
                
                cursor.execute('''
                    UPDATE members SET points = ?, total_consume = ?, level = ?
                    WHERE id = ?
                ''', (new_points, new_consume, new_level, sale['member_id']))
        
        # 记录退款操作
        cursor.execute('''
            INSERT INTO refunds (sale_id, reason, refund_amount, operator_id)
            VALUES (?, ?, ?, ?)
        ''', (sale_id, reason, sale['final_amount'], session['user_id']))
        
        refund_id = cursor.lastrowid
    
    return jsonify({'success': True, 'refund_id': refund_id, 'refund_amount': sale['final_amount']})

@app.route('/api/sale/<int:sale_id>/receipt')
def api_sale_receipt(sale_id):
    """获取小票HTML"""
    with get_db_cursor() as cursor:
        # 获取销售主记录
        cursor.execute('''
            SELECT s.*, e.name as cashier_name 
            FROM sales s 
            LEFT JOIN employees e ON s.cashier_id = e.id
            WHERE s.id = ?
        ''', (sale_id,))
        sale = cursor.fetchone()
        
        if not sale:
            return jsonify({'error': '订单不存在'}), 404
        
        sale = dict(sale)
        
        # 获取销售明细
        cursor.execute("SELECT * FROM sale_items WHERE sale_id = ?", (sale_id,))
        items = [dict(row) for row in cursor.fetchall()]
        
        # 获取会员信息
        member_info = None
        if sale['member_id']:
            cursor.execute("SELECT * FROM members WHERE id = ?", (sale['member_id'],))
            member = cursor.fetchone()
            if member:
                member_info = dict(member)
                member_info['level_name'] = get_member_level_name(member['level'])
        
        # 获取促销信息
        promotions_info = []
        now = int(time.time())
        cursor.execute("SELECT * FROM promotions WHERE status = 1 AND start_time <= ? AND end_time >= ?", (now, now))
        active_promos = [dict(row) for row in cursor.fetchall()]
        
        for promo in active_promos:
            promo_type_name = {0: '满减', 1: '折扣', 2: '赠品'}
            promotions_info.append(f"{promo['name']} ({promo_type_name.get(promo['type'], '未知')})")
    
    # 格式化时间
    sale_time = format_time(sale['created_at'])
    
    # 生成小票HTML
    receipt_html = f'''<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>购物小票 - #{sale_id}</title>
    <style>
        body {{ font-family: 'SimSun', '宋体', serif; width: 300px; margin: 0 auto; padding: 10px; }}
        .header {{ text-align: center; margin-bottom: 15px; }}
        .header h2 {{ margin: 5px 0; }}
        .header p {{ margin: 2px 0; font-size: 12px; }}
        .divider {{ border-top: 1px dashed #000; margin: 10px 0; }}
        .item {{ display: flex; justify-content: space-between; margin: 5px 0; }}
        .item-name {{ flex: 1; }}
        .item-detail {{ text-align: right; }}
        .total-section {{ margin-top: 10px; }}
        .total-row {{ display: flex; justify-content: space-between; margin: 3px 0; }}
        .total-final {{ font-weight: bold; font-size: 16px; border-top: 1px solid #000; padding-top: 5px; }}
        .footer {{ text-align: center; margin-top: 15px; font-size: 12px; }}
        .member-section {{ margin-top: 10px; }}
        @media print {{ body {{ margin: 0; padding: 5px; }}}}
    </style>
</head>
<body>
    <div class="header">
        <h2>超市管理系统</h2>
        <p>门店：总店</p>
        <p>订单号：{sale_id}</p>
        <p>日期时间：{sale_time}</p>
        <p>收银员：{sale.get('cashier_name', '未知')}</p>
    </div>
    
    <div class="divider"></div>
    
    <div class="items">
        <div class="item" style="font-weight: bold;">
            <span class="item-name">商品名称</span>
            <span class="item-detail">单价×数量 小计</span>
        </div>'''
    
    for item in items:
        receipt_html += f'''
        <div class="item">
            <span class="item-name">{item['product_name']}</span>
            <span class="item-detail">{item['price']:.2f}×{item['quantity']} {item['subtotal']:.2f}</span>
        </div>'''
    
    receipt_html += f'''
    </div>
    
    <div class="divider"></div>
    
    <div class="total-section">
        <div class="total-row">
            <span>原价：</span>
            <span>¥{sale['total_amount']:.2f}</span>
        </div>
        <div class="total-row">
            <span>优惠金额：</span>
            <span>-¥{sale['discount']:.2f}</span>
        </div>
        <div class="total-row total-final">
            <span>实收金额：</span>
            <span>¥{sale['final_amount']:.2f}</span>
        </div>
    </div>
    
    <div class="divider"></div>
    
    <div class="payment-section">
        <div class="total-row">
            <span>支付方式：</span>
            <span>{sale['payment_method']}</span>
        </div>'''
    
    if member_info:
        receipt_html += f'''
        <div class="total-row">
            <span>会员：</span>
            <span>{member_info['name']} ({member_info['level_name']})</span>
        </div>
        <div class="total-row">
            <span>会员积分：</span>
            <span>+{calculate_ladder_points(sale['final_amount'])}</span>
        </div>'''
    
    if promotions_info:
        receipt_html += '''
        <div class="member-section">'''
        for promo_text in promotions_info:
            receipt_html += f'''
            <div class="total-row">
                <span colspan="2">{promo_text}</span>
            </div>'''
        receipt_html += '''
        </div>'''
    
    receipt_html += f'''
    </div>
    
    <div class="footer">
        <div class="divider"></div>
        <p>欢迎下次光临！</p>
        <p>如有疑问请凭此小票咨询</p>
    </div>
</body>
</html>'''
    
    return receipt_html, 200, {'Content-Type': 'text/html; charset=utf-8'}

# ==================== 商品管理 ====================

@app.route('/products')
def products():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('products.html', username=session.get('username'))

@app.route('/api/product', methods=['POST'])
def api_product_create():
    """添加商品"""
    data = request.json
    with get_db_cursor() as cursor:
        cursor.execute('''
            INSERT INTO products (id, barcode, name, price, cost, stock, category)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (
            data['id'], data['barcode'], data['name'], 
            data['price'], data.get('cost', 0), data.get('stock', 0),
            data.get('category', '')
        ))
    return jsonify({'success': True})

@app.route('/api/product/<id>', methods=['PUT'])
def api_product_update(id):
    """更新商品"""
    data = request.json
    with get_db_cursor() as cursor:
        cursor.execute('''
            UPDATE products SET barcode = ?, name = ?, price = ?, cost = ?, stock = ?, category = ?
            WHERE id = ?
        ''', (data['barcode'], data['name'], data['price'], data.get('cost', 0), 
              data.get('stock', 0), data.get('category', ''), id))
    return jsonify({'success': True})

@app.route('/api/product/<id>', methods=['DELETE'])
def api_product_delete(id):
    """删除商品"""
    with get_db_cursor() as cursor:
        cursor.execute("DELETE FROM products WHERE id = ?", (id,))
    return jsonify({'success': True})

@app.route('/api/products/low-stock')
def api_products_low_stock():
    """获取库存预警商品列表（库存低于10件）"""
    threshold = 10
    with get_db_cursor() as cursor:
        cursor.execute(
            "SELECT * FROM products WHERE stock < ? ORDER BY stock ASC",
            (threshold,)
        )
        products = [dict(row) for row in cursor.fetchall()]
    return jsonify({'products': products, 'count': len(products), 'threshold': threshold})

# ==================== 会员管理 ====================

@app.route('/members')
def members():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('members.html', username=session.get('username'))

@app.route('/api/member', methods=['POST'])
def api_member_create():
    """添加会员"""
    data = request.json
    with get_db_cursor() as cursor:
        cursor.execute('''
            INSERT INTO members (name, phone, level, points)
            VALUES (?, ?, 0, 0)
        ''', (data['name'], data['phone']))
    return jsonify({'success': True})

@app.route('/api/members')
def api_members():
    """获取会员列表"""
    with get_db_cursor() as cursor:
        cursor.execute("SELECT * FROM members ORDER BY id DESC")
        members = [dict(row) for row in cursor.fetchall()]
    return jsonify(members)

@app.route('/api/member/<int:id>')
def api_member_detail(id):
    """获取会员详情"""
    with get_db_cursor() as cursor:
        cursor.execute("SELECT * FROM members WHERE id = ?", (id,))
        member = cursor.fetchone()
        if member:
            member = dict(member)
            member['level_name'] = get_member_level_name(member['level'])
            member['created_at_str'] = format_time(member['created_at'])
            member['last_consume_str'] = format_time(member.get('last_consume_at'))
            
            # 获取储值卡
            cursor.execute("SELECT * FROM vip_cards WHERE member_id = ?", (id,))
            cards = [dict(row) for row in cursor.fetchall()]
            member['vip_cards'] = cards
            
            return jsonify(member)
    return jsonify({'error': '会员不存在'}), 404

# ==================== 储值卡管理 ====================

@app.route('/vipcards')
def vipcards():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('vipcards.html', username=session.get('username'))

@app.route('/api/vipcard', methods=['POST'])
def api_vipcard_create():
    """创建储值卡"""
    data = request.json
    member_id = data.get('member_id')
    
    # 生成卡号
    card_no = f"VIP{int(time.time())}"
    salt = generate_salt()
    pwd_hash = hash_password(data.get('password', '123456'), salt)
    
    with get_db_cursor() as cursor:
        cursor.execute('''
            INSERT INTO vip_cards (card_no, member_id, password_hash, salt, balance)
            VALUES (?, ?, ?, ?, ?)
        ''', (card_no, member_id, pwd_hash, salt, data.get('balance', 0)))
        
        if member_id and data.get('balance', 0) > 0:
            cursor.execute(
                "INSERT INTO vip_card_tx (card_no, type, amount, balance_before, operator_id) VALUES (?, 0, ?, 0, ?)",
                (card_no, data.get('balance', 0), session['user_id'])
            )
    
    return jsonify({'success': True, 'card_no': card_no})

@app.route('/api/vipcard/recharge', methods=['POST'])
def api_vipcard_recharge():
    """储值卡充值"""
    data = request.json
    card_no = data['card_no']
    amount = data['amount']
    
    with get_db_cursor() as cursor:
        cursor.execute("SELECT * FROM vip_cards WHERE card_no = ?", (card_no,))
        card = cursor.fetchone()
        
        if card:
            new_balance = card['balance'] + amount
            cursor.execute("UPDATE vip_cards SET balance = ? WHERE card_no = ?", (new_balance, card_no))
            cursor.execute(
                "INSERT INTO vip_card_tx (card_no, type, amount, balance_before, operator_id) VALUES (?, 0, ?, ?, ?)",
                (card_no, amount, card['balance'], session['user_id'])
            )
            return jsonify({'success': True, 'balance': new_balance})
    
    return jsonify({'error': '储值卡不存在'}), 404

@app.route('/api/vipcards')
def api_vipcards():
    """获取储值卡列表"""
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT v.*, m.name as member_name 
            FROM vip_cards v 
            LEFT JOIN members m ON v.member_id = m.id
            ORDER BY v.id DESC
        ''')
        cards = [dict(row) for row in cursor.fetchall()]
    return jsonify(cards)

# ==================== 报表 ====================

@app.route('/reports')
def reports():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('reports.html', username=session.get('username'))

@app.route('/api/reports/sales')
def api_reports_sales():
    """销售报表"""
    days = int(request.args.get('days', 30))
    start_time = datetime.now() - timedelta(days=days)
    start_ts = start_time.replace(hour=0, minute=0, second=0).timestamp()
    
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT s.*, e.name as cashier_name 
            FROM sales s 
            LEFT JOIN employees e ON s.cashier_id = e.id
            WHERE s.created_at >= ? AND s.status = 1
            ORDER BY s.created_at DESC
        ''', (start_ts,))
        sales = [dict(row) for row in cursor.fetchall()]
    
    # 统计
    total_amount = sum(s['total_amount'] for s in sales)
    total_final = sum(s['final_amount'] for s in sales)
    total_discount = sum(s['discount'] for s in sales)
    
    # 按支付方式统计
    payment_stats = {}
    for s in sales:
        method = s['payment_method'] or '其他'
        if method not in payment_stats:
            payment_stats[method] = {'count': 0, 'amount': 0}
        payment_stats[method]['count'] += 1
        payment_stats[method]['amount'] += s['final_amount']
    
    return jsonify({
        'sales': sales,
        'stats': {
            'total_amount': total_amount,
            'total_final': total_final,
            'total_discount': total_discount,
            'count': len(sales),
            'payment_stats': payment_stats
        }
    })

@app.route('/api/reports/export/csv')
def api_reports_export_csv():
    """导出CSV"""
    days = int(request.args.get('days', 30))
    start_time = datetime.now() - timedelta(days=days)
    start_ts = start_time.replace(hour=0, minute=0, second=0).timestamp()
    
    with get_db_cursor() as cursor:
        cursor.execute('''
            SELECT s.id, s.cashier_id, s.created_at, s.total_amount, s.discount, 
                   s.final_amount, s.payment_method, s.status
            FROM sales s
            WHERE s.created_at >= ? AND s.status = 1
            ORDER BY s.created_at DESC
        ''', (start_ts,))
        sales = cursor.fetchall()
    
    # 生成CSV
    csv = '\ufeff订单号,收银员ID,时间,原价,优惠,实收,支付方式,状态\n'
    for s in sales:
        time_str = format_time(s['created_at'])
        status_str = '已完成' if s['status'] == 1 else '已退款'
        csv += f"{s['id']},{s['cashier_id']},{time_str},{s['total_amount']:.2f},{s['discount']:.2f},{s['final_amount']:.2f},{s['payment_method']},{status_str}\n"
    
    filename = f"sales_report_{int(time.time())}.csv"
    with open(filename, 'w', encoding='utf-8-sig') as f:
        f.write(csv)
    
    return send_file(filename, as_attachment=True)

# ==================== 促销管理 ====================

@app.route('/promotions')
def promotions():
    if 'user_id' not in session:
        return redirect(url_for('login'))
    return render_template('promotions.html', username=session.get('username'))

@app.route('/api/promotion', methods=['POST'])
def api_promotion_create():
    """添加促销"""
    data = request.json
    with get_db_cursor() as cursor:
        cursor.execute('''
            INSERT INTO promotions (name, type, value, start_time, end_time)
            VALUES (?, ?, ?, ?, ?)
        ''', (data['name'], data['type'], data['value'], data['start_time'], data['end_time']))
    return jsonify({'success': True})

@app.route('/api/promotions')
def api_promotions():
    """获取促销列表"""
    with get_db_cursor() as cursor:
        cursor.execute("SELECT * FROM promotions WHERE status = 1 ORDER BY id DESC")
        promos = [dict(row) for row in cursor.fetchall()]
    return jsonify(promos)

# ==================== 初始化 ====================

if __name__ == '__main__':
    init_db()
    print("=" * 50)
    print("  超市管理系统 Web 版")
    print("  访问地址: http://127.0.0.1:5000")
    print("  默认账号: admin / admin123")
    print("=" * 50)
    app.run(debug=True, host='0.0.0.0', port=5000)
