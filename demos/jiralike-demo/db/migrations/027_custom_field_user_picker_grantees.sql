-- Migration 027: Custom Field User Picker Grantees and work_item_custom_fields View

CREATE VIEW IF NOT EXISTS work_item_custom_fields AS
SELECT id, work_item_id, custom_field_id,
       CASE
         WHEN user_value_id IS NOT NULL AND user_value_id > 0 THEN CAST(user_value_id AS TEXT)
         ELSE COALESCE(text_value, '')
       END AS value
FROM custom_field_values;
