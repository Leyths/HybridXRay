-- Editor preview pass: alpha-only sampling so the editor renders the particle
-- with its texture's alpha channel instead of an opaque square. In-game the
-- distortion pass (l_special, below) is what's actually composited; this E[0]
-- entry just gives ParticleEditor / LevelEditor a faithful blend-state preview
-- (mirrors how models_xdistort.s pairs a normal alphaonly pass with l_special).
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("particle",	"particle_alphaonly")
			: sorting	(3, false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: aref 		(true,0)
			: zb 		(true,false)
			: fog		(false)
			: distort 	(true)
	shader:sampler	("s_base")      :texture	(t_base)
end

function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("particle",	"particle_distort")
			: sorting	(3, false)
			: blend		(true,blend.srcalpha,blend.invsrcalpha)
			: zb 		(true,false)
			: fog		(false)
			: distort 	(true)
	shader:sampler	("s_base")      :texture	(t_base)
	shader:sampler	("s_distort")   :texture	(t_base)	-- "pfx\\pfx_distortion"
end
