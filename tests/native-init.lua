-- Isolated acceptance module only. Loads real native sheets through public APIs.
local M={}
function M:enable()
    local gm=modules.gmResourceModifier
    local a=gm:LoadGm1Resource('gm/body_missile.gm1')
    local b=gm:LoadGm1Resource('gm/body_missile_cow.gm1')
    assert(a>=0 and b>=0,'native test sheets did not load')
    local tokens={gm:ReserveGm(34,a),gm:ReserveGm(135,b),gm:ReserveGm(34,a)}
    assert(tokens[1]>=0 and tokens[2]>=0 and tokens[3]>=0,'reservation request failed')
    assert(gm:GetReservedGm(tokens[1])==-1,'native ID exposed before admission')
    assert(not gm:FreeGm1Resource(a) and not gm:FreeGm1Resource(b),'pending resources were not pinned')
    assert(gm:SetGm(34,-1,a,-1),'existing texture replacement queue failed')
    hooks.registerHookCallback('afterInit',function()
        local slots={}
        for i,t in ipairs(tokens) do slots[i]=gm:GetReservedGm(t) end
        if slots[1]<0 or slots[2]~=slots[1]+1 or slots[3]~=slots[2]+1 then
            log(FATAL,'[gm-sheet-test] FAIL: native reservation batch was not admitted')
            return
        end
        assert(not gm:FreeGm1Resource(a) and not gm:FreeGm1Resource(b),'active resource refs missing')
        assert(gm:SetGm(slots[1],-1,-1,-1),'first inherited reset failed')
        assert(not gm:FreeGm1Resource(a),'shared resource freed while another sheet uses it')
        assert(gm:SetGm(slots[3],-1,-1,-1),'second inherited reset failed')
        assert(not gm:FreeGm1Resource(a),'queued texture reference was lost')
        assert(gm:SetGm(34,-1,-1,-1) and gm:FreeGm1Resource(a),'original texture reset/free failed')
        assert(gm:SetGm(slots[2],-1,-1,-1) and gm:FreeGm1Resource(b),'cow inherited reset/free failed')
        log(INFO,'[gm-sheet-test] PASS: native sheets '..table.concat(slots,',')..'; shared pins, queued texture, inherited resets and free verified')
    end)
end
return M
