USE chat;

DELETE FROM ticket_refunds;
DELETE FROM ticket_orders;

UPDATE ticket_inventory
SET total_qty = CASE
        WHEN ticket_type_id = 1001 THEN 200
        ELSE total_qty
    END,
    remaining_qty = CASE
        WHEN ticket_type_id = 1001 THEN 200
        ELSE total_qty
    END
WHERE ticket_type_id IN (1001, 1002, 1003);
