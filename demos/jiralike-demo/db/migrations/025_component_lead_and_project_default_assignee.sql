-- Migration 025: Component lead and project default assignee routing

-- 1. Add default_assignee_type column to projects table
ALTER TABLE projects ADD COLUMN default_assignee_type TEXT NOT NULL DEFAULT 'PROJECT_LEAD';

-- 2. Normalize components default_assignee_type to uppercase
UPDATE components
SET default_assignee_type = CASE
    WHEN upper(default_assignee_type) = 'COMPONENT_LEAD' THEN 'COMPONENT_LEAD'
    WHEN upper(default_assignee_type) = 'PROJECT_DEFAULT' THEN 'PROJECT_DEFAULT'
    WHEN upper(default_assignee_type) = 'PROJECT_LEAD' THEN 'PROJECT_LEAD'
    WHEN upper(default_assignee_type) = 'UNASSIGNED' THEN 'UNASSIGNED'
    ELSE 'PROJECT_DEFAULT'
END
WHERE default_assignee_type IS NOT NULL;
