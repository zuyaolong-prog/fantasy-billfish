'use strict';

const types = new Set(['object', 'array', 'string', 'integer', 'number', 'boolean', 'null']);
const keywords = new Set(['type', 'properties', 'required', 'additionalProperties', 'items', 'minItems', 'maxItems',
  'minLength', 'maxLength', 'minimum', 'maximum', 'enum', 'title', 'description', 'default']);
const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);

function validateSchema(schema, depth = 0) {
  if (!object(schema) || depth > 16 || !types.has(schema.type)) throw new Error('Invalid schema type or nesting depth');
  for (const key of Object.keys(schema)) if (!keywords.has(key)) throw new Error(`Unsupported schema keyword: ${key}`);
  for (const key of ['title', 'description']) if (key in schema && typeof schema[key] !== 'string') throw new Error(`${key} must be a string`);
  if ('enum' in schema && (!Array.isArray(schema.enum) || !schema.enum.length)) throw new Error('enum must be a non-empty array');
  const groups = {properties:'object', required:'object', additionalProperties:'object', items:'array', minItems:'array', maxItems:'array', minLength:'string', maxLength:'string'};
  for (const [key, kind] of Object.entries(groups)) if (key in schema && schema.type !== kind) throw new Error(`${key} requires ${kind}`);
  for (const key of ['minItems', 'maxItems', 'minLength', 'maxLength']) if (key in schema && (!Number.isInteger(schema[key]) || schema[key] < 0)) throw new Error(`${key} must be a non-negative integer`);
  for (const key of ['minimum', 'maximum']) if (key in schema && (!['integer','number'].includes(schema.type) || !Number.isFinite(schema[key]))) throw new Error(`${key} requires a finite numeric limit`);
  for (const [low, high] of [['minItems','maxItems'],['minLength','maxLength'],['minimum','maximum']]) if (low in schema && high in schema && schema[low] > schema[high]) throw new Error(`${low} exceeds ${high}`);
  if ('properties' in schema && !object(schema.properties)) throw new Error('properties must be an object');
  for (const child of Object.values(schema.properties || {})) validateSchema(child, depth + 1);
  if ('required' in schema && (!Array.isArray(schema.required) || schema.required.some(key => typeof key !== 'string' || !Object.hasOwn(schema.properties || {}, key)) || new Set(schema.required).size !== schema.required.length)) throw new Error('required must list unique declared properties');
  if ('additionalProperties' in schema && typeof schema.additionalProperties !== 'boolean') throw new Error('additionalProperties must be boolean');
  if ('items' in schema) validateSchema(schema.items, depth + 1);
}

function validate(value, schema, path = 'arguments') {
  const kind = schema.type;
  const matches = {object:object(value), array:Array.isArray(value), string:typeof value === 'string',
    integer:Number.isInteger(value), number:typeof value === 'number' && Number.isFinite(value), boolean:typeof value === 'boolean', null:value === null};
  function fail(message) { const error = new Error(`${path}: ${message}`); error.code = 'invalid_arguments'; throw error; }
  if (!matches[kind]) fail(`expected ${kind}`);
  if (schema.enum && !schema.enum.some(candidate => require('node:util').isDeepStrictEqual(value, candidate))) fail('value is not in enum');
  if (kind === 'object') {
    const properties = schema.properties || {};
    for (const key of schema.required || []) if (!Object.hasOwn(value, key)) fail(`missing required ${key}`);
    for (const [key, item] of Object.entries(value)) {
      if (Object.hasOwn(properties, key)) validate(item, properties[key], `${path}.${key}`);
      else if (schema.additionalProperties === false) fail(`additional property ${key} is not allowed`);
    }
  } else if (kind === 'array' || kind === 'string') {
    const count = kind === 'string' ? [...value].length : value.length;
    const low = kind === 'array' ? 'minItems' : 'minLength';
    const high = kind === 'array' ? 'maxItems' : 'maxLength';
    if ((low in schema && count < schema[low]) || (high in schema && count > schema[high])) fail('length is outside allowed range');
    if (kind === 'array' && schema.items) value.forEach((item, index) => validate(item, schema.items, `${path}[${index}]`));
  } else if (kind === 'number' || kind === 'integer') {
    if (('minimum' in schema && value < schema.minimum) || ('maximum' in schema && value > schema.maximum)) fail('number is outside allowed range');
  }
}

module.exports = {validate, validateSchema};
