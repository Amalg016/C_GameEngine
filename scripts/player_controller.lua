-- scripts/player_controller.lua
--
-- Per-entity script component: WASD keyboard movement.
-- Demonstrates the engine's input system.

local PlayerController = {}

function PlayerController:on_init()
    self.speed = 5.0
    self.bound = 5.0
    engine.log("[PlayerController] ready on entity " .. self.entity
               .. " — use WASD to move")
end

function PlayerController:on_update(dt)
    local x, y, sx, sy = engine.get_transform(self.entity)

    -- WASD movement.
    if engine.is_key_down(engine.key.W) or engine.is_key_down(engine.key.UP) then
        y = y - self.speed * dt
    end
    if engine.is_key_down(engine.key.S) or engine.is_key_down(engine.key.DOWN) then
        y = y + self.speed * dt
    end
    if engine.is_key_down(engine.key.A) or engine.is_key_down(engine.key.LEFT) then
        x = x - self.speed * dt
    end
    if engine.is_key_down(engine.key.D) or engine.is_key_down(engine.key.RIGHT) then
        x = x + self.speed * dt
    end

    -- Clamp to world bounds.
    local hw, hh = sx * 0.5, sy * 0.5
    if x + hw > self.bound then x = self.bound - hw end
    if x - hw < -self.bound then x = -self.bound + hw end
    if y + hh > self.bound then y = self.bound - hh end
    if y - hh < -self.bound then y = -self.bound + hh end

    engine.set_transform(self.entity, x, y, sx, sy)
end

function PlayerController:on_destroy()
    engine.log("[PlayerController] detached from entity " .. self.entity)
end

return PlayerController
