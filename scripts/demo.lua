-- ---------------------------------------------------------------------------
-- Demo Scene — Lua equivalent of the hardcoded setup in main.c.
--
-- Spawns a camera, loads a texture, creates a parent sprite with a velocity
-- component (bounces via the C movement system), and attaches a child sprite.
-- ---------------------------------------------------------------------------

local parent, child, tex

function on_init()
    engine.log("[lua] on_init — setting up scene")

    -- Create camera entity.
    local cam = engine.create_entity()
    engine.set_transform(cam, 0, 0, 1, 1)
    engine.set_camera_ortho(cam, 5.0)
    engine.log(string.format("[lua] camera entity=%d (orthographic, ortho_size=5.0)", cam))

    -- Load texture.
    tex = engine.load_texture("assets/images/file.png")
    engine.log(string.format("[lua] texture loaded (handle=%d)", tex))

    -- Spawn parent sprite: 2×2 world units at origin.
    parent = engine.create_entity()
    engine.set_transform(parent, 0, 0, 2, 2)
    engine.set_sprite(parent, tex)
    engine.set_velocity(parent, 3, 2)

    -- Spawn child sprite: half-size, offset from parent.
    child = engine.create_entity()
    engine.set_transform(child, 2, 1.5, 0.5, 0.5)
    engine.set_sprite(child, tex)
    engine.set_parent(child, parent)

    engine.log(string.format("[lua] parent=%d  child=%d (attached)", parent, child))
    engine.log("[lua] scene ready")
end

function on_update(dt)
    -- Future: input handling, animation, etc.
end
