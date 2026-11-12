-- =====================================================================
-- ClimControl · 2) Vincular un aparato sin tocar Arduino IDE
--
-- Pegar en Supabase → SQL Editor → Run (después de climcontrol.sql).
-- Se puede volver a correr sin perder datos.
--
-- Cómo funciona: la app genera un código corto (8 letras y números, vence
-- en 30 minutos). El cliente lo escribe en la página del aparato junto con
-- su WiFi. El aparato llama a vincular_dispositivo con ese código y recibe
-- su clave propia, que guarda en su memoria.
-- =====================================================================

alter table public.dispositivos add column if not exists vinculacion text;
alter table public.dispositivos add column if not exists vinculacion_vence timestamptz;
create unique index if not exists dispositivos_vinculacion on public.dispositivos (vinculacion) where vinculacion is not null;
grant select (vinculacion, vinculacion_vence) on public.dispositivos to authenticated;

-- 8 caracteres sin los que se confunden (0/O, 1/I/L): 31^8 combinaciones
create or replace function private.codigo_vinculacion() returns text
language plpgsql volatile set search_path = '' as $$
declare
  letras constant text := '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
  b bytea; c text; pos int;
begin
  loop
    b := uuid_send(gen_random_uuid());
    c := '';
    foreach pos in array array[0, 1, 2, 3, 4, 5, 10, 11] loop
      c := c || substr(letras, (get_byte(b, pos) % 31) + 1, 1);
    end loop;
    exit when not exists (select 1 from public.dispositivos d where d.vinculacion = c);
  end loop;
  return c;
end $$;

-- Crear un aula: ahora también devuelve el código de vinculación
create or replace function public.crear_aula(p_nombre text) returns json
language plpgsql security definer set search_path = '' as $$
declare
  v_uid    uuid := auth.uid();
  v_nombre text := trim(coalesce(p_nombre, ''));
  v_aula   bigint;
  v_codigo text := private.codigo_nuevo();
  v_clave  text := private.clave_nueva();
  v_vinc   text := private.codigo_vinculacion();
  v_vence  timestamptz := now() + interval '30 minutes';
begin
  if v_uid is null then raise exception 'SIN_SESION' using errcode = '28000'; end if;
  if char_length(v_nombre) not between 1 and 60 then raise exception 'NOMBRE_INVALIDO' using errcode = '22023'; end if;
  if exists (select 1 from public.aulas a where a.usuario_id = v_uid and lower(a.nombre) = lower(v_nombre)) then
    raise exception 'NOMBRE_REPETIDO' using errcode = '23505';
  end if;
  if (select count(*) from public.aulas a where a.usuario_id = v_uid) >= 50 then
    raise exception 'DEMASIADAS_AULAS' using errcode = '54000';
  end if;
  insert into public.aulas (usuario_id, nombre) values (v_uid, v_nombre) returning id into v_aula;
  insert into public.dispositivos (aula_id, codigo, clave_hash, vinculacion, vinculacion_vence)
  values (v_aula, v_codigo, private.hash_clave(v_clave), v_vinc, v_vence);
  return json_build_object('aula_id', v_aula, 'codigo', v_codigo, 'clave', v_clave, 'vinculacion', v_vinc, 'vence', v_vence);
end $$;

-- Generar un código nuevo para vincular (o volver a vincular) el aparato de un aula
create or replace function public.generar_codigo_vinculacion(p_aula bigint) returns json
language plpgsql security definer set search_path = '' as $$
declare v_vinc text := private.codigo_vinculacion(); v_vence timestamptz := now() + interval '30 minutes';
begin
  if not exists (select 1 from public.aulas a where a.id = p_aula and a.usuario_id = auth.uid()) then
    raise exception 'AULA_INEXISTENTE' using errcode = 'P0002';
  end if;
  insert into public.dispositivos as d (aula_id, codigo, clave_hash, vinculacion, vinculacion_vence)
  values (p_aula, private.codigo_nuevo(), private.hash_clave(private.clave_nueva()), v_vinc, v_vence)
  on conflict (aula_id) do update set vinculacion = excluded.vinculacion, vinculacion_vence = excluded.vinculacion_vence;
  return json_build_object('vinculacion', v_vinc, 'vence', v_vence);
end $$;

-- El aparato canjea el código por su clave propia (sin cuenta: usa la clave pública)
create or replace function public.vincular_dispositivo(p_vinculacion text) returns json
language plpgsql security definer set search_path = '' as $$
declare
  v_vinc   text := upper(regexp_replace(coalesce(p_vinculacion, ''), '[^A-Za-z0-9]', '', 'g'));
  v_clave  text := private.clave_nueva();
  v_id     bigint;
  v_codigo text;
begin
  update public.dispositivos d
     set clave_hash = private.hash_clave(v_clave), vinculacion = null, vinculacion_vence = null, ultimo_contacto = now()
   where d.vinculacion = v_vinc and d.vinculacion_vence > now()
  returning d.id, d.codigo into v_id, v_codigo;
  if v_id is null then raise exception 'VINCULACION_INVALIDA' using errcode = '28000'; end if;
  return json_build_object('codigo', v_codigo, 'clave', v_clave);
end $$;

revoke execute on all functions in schema private from public, anon, authenticated;
revoke execute on function public.crear_aula(text), public.generar_codigo_vinculacion(bigint) from public, anon;
grant execute on function public.crear_aula(text), public.generar_codigo_vinculacion(bigint) to authenticated;
revoke execute on function public.vincular_dispositivo(text) from public;
grant execute on function public.vincular_dispositivo(text) to anon, authenticated;

-- El estado de cada aula también dice si hay una vinculación pendiente
create or replace view public.estado_aulas with (security_invoker = true) as
select a.id as aula_id, a.nombre, d.codigo, d.ultimo_contacto,
       l.fecha_hora, l.temperatura, l.humedad, l.hay_personas, l.ventilador, l.aire, l.luces,
       d.vinculacion, d.vinculacion_vence
  from public.aulas a
  left join public.dispositivos d on d.aula_id = a.id
  left join lateral (select x.fecha_hora, x.temperatura, x.humedad, x.hay_personas, x.ventilador, x.aire, x.luces
                       from public.lecturas x where x.aula_id = a.id
                      order by x.fecha_hora desc limit 1) l on true;
revoke all on public.estado_aulas from anon;
grant select on public.estado_aulas to authenticated;
