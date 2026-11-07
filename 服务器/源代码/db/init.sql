USE chat;

CREATE TABLE IF NOT EXISTS ticket_users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(32) NOT NULL,
    password_hash CHAR(64) NOT NULL,
    password_salt CHAR(32) NOT NULL,
    login_fail_count INT NOT NULL DEFAULT 0,
    lock_until TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_ticket_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 兼容已经在跑的旧库：给 ticket_users 补上登录失败计数和锁定时间。
ALTER TABLE ticket_users
    ADD COLUMN IF NOT EXISTS login_fail_count INT NOT NULL DEFAULT 0;
ALTER TABLE ticket_users
    ADD COLUMN IF NOT EXISTS lock_until TIMESTAMP NULL DEFAULT NULL;

CREATE TABLE IF NOT EXISTS ticket_inventory (
    ticket_type_id INT UNSIGNED NOT NULL,
    event_name VARCHAR(128) NOT NULL,
    ticket_name VARCHAR(128) NOT NULL,
    price_cents INT UNSIGNED NOT NULL,
    total_qty INT UNSIGNED NOT NULL,
    remaining_qty INT UNSIGNED NOT NULL,
    PRIMARY KEY (ticket_type_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS ticket_orders (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    order_no VARCHAR(64) NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    ticket_type_id INT UNSIGNED NOT NULL,
    quantity INT UNSIGNED NOT NULL,
    refunded_qty INT UNSIGNED NOT NULL DEFAULT 0,
    amount_cents INT UNSIGNED NOT NULL,
    status ENUM('PENDING_PAY','PAID','PARTIALLY_REFUNDED','REFUNDED',
                'CANCELLED','EXPIRED') NOT NULL DEFAULT 'PENDING_PAY',
    idempotency_key VARCHAR(64) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_ticket_orders_order_no (order_no),
    UNIQUE KEY uk_ticket_orders_user_idem (user_id, idempotency_key),
    KEY idx_ticket_orders_user (user_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

ALTER TABLE ticket_orders
    ADD COLUMN IF NOT EXISTS refunded_qty INT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE ticket_orders
    MODIFY COLUMN status ENUM('PENDING_PAY','PAID','PARTIALLY_REFUNDED',
                              'REFUNDED','CANCELLED','EXPIRED')
    NOT NULL DEFAULT 'PENDING_PAY';

CREATE TABLE IF NOT EXISTS ticket_refunds (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    refund_no VARCHAR(64) NOT NULL,
    original_order_no VARCHAR(64) NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    ticket_type_id INT UNSIGNED NOT NULL,
    quantity INT UNSIGNED NOT NULL,
    request_id VARCHAR(64) NOT NULL,
    status ENUM('COMMITTED') NOT NULL DEFAULT 'COMMITTED',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_ticket_refunds_refund_no (refund_no),
    UNIQUE KEY uk_ticket_refunds_user_request (user_id, request_id),
    KEY idx_ticket_refunds_order (original_order_no),
    KEY idx_ticket_refunds_user (user_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO ticket_inventory
    (ticket_type_id, event_name, ticket_name, price_cents, total_qty, remaining_qty)
VALUES
    (1001, 'Demo Concert', 'Early Bird', 38000, 200, 200),
    (1002, 'Demo Concert', 'General Admission', 58000, 500, 500),
    (1003, 'Demo Sports', 'Stand A', 88000, 50, 50)
ON DUPLICATE KEY UPDATE
    event_name = VALUES(event_name),
    ticket_name = VALUES(ticket_name),
    price_cents = VALUES(price_cents),
    total_qty = VALUES(total_qty),
    remaining_qty = VALUES(remaining_qty);
