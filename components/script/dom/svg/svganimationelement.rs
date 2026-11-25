/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Implementation of SVG SMIL animation elements.
//!
//! This is a minimal implementation of the SMIL animation timing model. It
//! supports `<animate>`, `<set>`, `<animateTransform>` and `<animateColor>`
//! with the `attributeName`, `from`/`to`/`by`/`values`, and
//! `dur`/`begin`/`fill`/`repeatCount`/`calcMode`/`additive`/`accumulate`
//! attributes. Values are applied by mutating the target element's attributes
//! so that the animated value is visible to style, layout and script.

use std::cell::{Cell, RefCell};

use dom_struct::dom_struct;
use html5ever::{LocalName, Prefix, local_name, ns};
use js::context::JSContext;
use js::rust::HandleObject;
use style::attr::AttrValue;

use crate::dom::bindings::inheritance::Castable;
use crate::dom::bindings::root::{Dom, DomRoot};
use crate::dom::bindings::str::DOMString;
use crate::dom::document::Document;
use crate::dom::element::attributes::storage::AttrRef;
use crate::dom::element::{AttributeMutation, Element};
use crate::dom::node::{Node, NodeDamage, NodeTraits, VirtualMethods};
use crate::dom::svg::svgelement::SVGElement;

/// The kind of value that an animation interpolates.
#[derive(Clone, Debug, PartialEq)]
enum AnimatedValue {
    /// A single floating point number.
    Number(f64),
    /// A list of floating point numbers.
    NumberList(Vec<f64>),
    /// An arbitrary string, animated discretely.
    String(String),
}

impl AnimatedValue {
    fn parse(value: &str) -> Option<AnimatedValue> {
        let value = value.trim();
        if value.is_empty() {
            return None;
        }
        if let Ok(number) = value.parse::<f64>() {
            return Some(AnimatedValue::Number(number));
        }
        let numbers: Option<Vec<f64>> = value
            .split_whitespace()
            .map(|part| part.parse::<f64>())
            .collect();
        if let Some(numbers) = numbers {
            return Some(AnimatedValue::NumberList(numbers));
        }
        Some(AnimatedValue::String(value.to_string()))
    }

    /// Linearly interpolate between two values. Non-numeric values fall back
    /// to discrete interpolation.
    fn interpolate(from: &AnimatedValue, to: &AnimatedValue, progress: f64) -> AnimatedValue {
        let progress = progress.clamp(0.0, 1.0);
        match (from, to) {
            (AnimatedValue::Number(from), AnimatedValue::Number(to)) => {
                AnimatedValue::Number(from + (to - from) * progress)
            },
            (AnimatedValue::NumberList(from), AnimatedValue::NumberList(to))
                if from.len() == to.len() =>
            {
                AnimatedValue::NumberList(
                    from.iter()
                        .zip(to.iter())
                        .map(|(from, to)| from + (to - from) * progress)
                        .collect(),
                )
            },
            _ => {
                if progress < 1.0 {
                    from.clone()
                } else {
                    to.clone()
                }
            },
        }
    }

    fn to_attr_value(&self) -> AttrValue {
        let value = match self {
            AnimatedValue::Number(number) => format_number(*number),
            AnimatedValue::NumberList(numbers) => numbers
                .iter()
                .map(|number| format_number(*number))
                .collect::<Vec<_>>()
                .join(" "),
            AnimatedValue::String(string) => string.clone(),
        };
        AttrValue::String(DOMString::from(value).into())
    }

    /// Add two values together, for the `additive="sum"` and `accumulate="sum"`
    /// behaviours.
    fn add(&self, other: &AnimatedValue) -> AnimatedValue {
        match (self, other) {
            (AnimatedValue::Number(a), AnimatedValue::Number(b)) => AnimatedValue::Number(a + b),
            (AnimatedValue::NumberList(a), AnimatedValue::NumberList(b)) if a.len() == b.len() => {
                AnimatedValue::NumberList(a.iter().zip(b.iter()).map(|(a, b)| a + b).collect())
            },
            _ => other.clone(),
        }
    }

    /// A value suitable for adding to an attribute parsed from a string.
    fn from_base(base: &str) -> Option<AnimatedValue> {
        AnimatedValue::parse(&base.trim().replace(',', " "))
    }
}

/// Format a number the way SVG attribute serializers do, avoiding a trailing
/// `.0` for integral values.
fn format_number(number: f64) -> String {
    if number.fract() == 0.0 && number.abs() < 1e15 {
        format!("{}", number as i64)
    } else {
        number.to_string()
    }
}

/// Parse a clock value such as `2s`, `500ms`, `1.5s`, `1min` or a bare number
/// of seconds, returning the number of seconds.
fn parse_clock_value(value: &str) -> Option<f64> {
    let value = value.trim();
    if let Some(number) = value.strip_suffix("ms") {
        return number.trim().parse::<f64>().ok().map(|number| number / 1000.0);
    }
    if let Some(number) = value.strip_suffix('s') {
        return number.trim().parse::<f64>().ok();
    }
    if let Some(number) = value.strip_suffix("min") {
        return number.trim().parse::<f64>().ok().map(|number| number * 60.0);
    }
    if let Some(number) = value.strip_suffix('h') {
        return number.trim().parse::<f64>().ok().map(|number| number * 3600.0);
    }
    value.parse::<f64>().ok()
}

/// The interpolation mode of an animation.
#[derive(Clone, Copy, Debug, PartialEq)]
enum CalcMode {
    Discrete,
    Linear,
    Paced,
    Spline,
}

impl CalcMode {
    fn parse(value: &str) -> CalcMode {
        match value.trim() {
            "discrete" => CalcMode::Discrete,
            "paced" => CalcMode::Paced,
            "spline" => CalcMode::Spline,
            _ => CalcMode::Linear,
        }
    }
}

/// The fill behaviour of an animation.
#[derive(Clone, Copy, Debug, PartialEq)]
enum FillMode {
    Remove,
    Freeze,
}

/// The additive behaviour of an animation.
#[derive(Clone, Copy, Debug, PartialEq)]
enum AdditiveMode {
    Replace,
    Sum,
}

/// The parsed state of a SMIL animation element.
#[derive(Debug)]
struct AnimationState {
    /// The name of the attribute being animated.
    attribute_name: Option<LocalName>,
    /// The value at the start of the animation.
    from: Option<AnimatedValue>,
    /// The value at the end of the animation.
    to: Option<AnimatedValue>,
    /// The relative offset used when `from` is absent.
    by: Option<AnimatedValue>,
    /// The explicit list of values.
    values: Option<Vec<AnimatedValue>>,
    /// The duration of a single iteration in seconds.
    dur: Option<f64>,
    /// The start time in seconds relative to the document timeline.
    begin: Option<f64>,
    /// The number of iterations. `Some(f64::INFINITY)` means repeat forever.
    repeat_count: Option<f64>,
    /// The fill behaviour.
    fill: FillMode,
    /// The interpolation mode.
    calc_mode: CalcMode,
    /// Whether the animation adds to the underlying value.
    additive: AdditiveMode,
    /// Whether repeated iterations accumulate.
    accumulate: bool,
    /// The value of the target attribute before the animation was applied.
    base_value: Option<AttrValue>,
    /// Whether the animation is currently being applied.
    applied: bool,
    /// The last document time at which the animation was applied, used to avoid
    /// redundant work.
    last_applied_time: Option<f64>,
}

impl AnimationState {
    fn new() -> AnimationState {
        AnimationState {
            attribute_name: None,
            from: None,
            to: None,
            by: None,
            values: None,
            dur: None,
            begin: None,
            repeat_count: None,
            fill: FillMode::Remove,
            calc_mode: CalcMode::Linear,
            additive: AdditiveMode::Replace,
            accumulate: false,
            base_value: None,
            applied: false,
            last_applied_time: None,
        }
    }

    /// The total duration of the animation, accounting for repetitions, in
    /// seconds.
    fn total_duration(&self) -> Option<f64> {
        let dur = self.dur?;
        if dur <= 0.0 {
            return Some(0.0);
        }
        match self.repeat_count {
            Some(count) if count.is_infinite() => Some(f64::INFINITY),
            Some(count) => Some(dur * count.max(0.0)),
            None => Some(dur),
        }
    }

    /// Whether the animation is contributing a value at the given elapsed time.
    fn is_active_at(&self, elapsed: f64) -> bool {
        let Some(total_duration) = self.total_duration() else {
            return false;
        };
        if elapsed < total_duration {
            return true;
        }
        // At or past the end of the active duration, only `fill="freeze"`
        // contributes, and only for a finite duration.
        self.fill == FillMode::Freeze && total_duration.is_finite()
    }
}

/// A SMIL animation element.
#[dom_struct]
pub(crate) struct SVGAnimationElement {
    svgelement: SVGElement,
    /// The parsed animation state, rebuilt whenever the element's attributes
    /// change.
    state: RefCell<AnimationState>,
}

impl SVGAnimationElement {
    fn new_inherited(
        local_name: LocalName,
        prefix: Option<Prefix>,
        document: &Document,
    ) -> SVGAnimationElement {
        SVGAnimationElement {
            svgelement: SVGElement::new_inherited_with_state(
                Default::default(),
                local_name,
                prefix,
                document,
            ),
            state: RefCell::new(AnimationState::new()),
        }
    }

    pub(crate) fn new(
        cx: &mut JSContext,
        local_name: LocalName,
        prefix: Option<Prefix>,
        document: &Document,
        proto: Option<HandleObject>,
    ) -> DomRoot<SVGAnimationElement> {
        Node::reflect_node_with_proto(
            cx,
            Box::new(SVGAnimationElement::new_inherited(
                local_name, prefix, document,
            )),
            document,
            proto,
        )
    }

    /// The element that this animation targets: its parent element.
    fn target(&self) -> Option<DomRoot<Element>> {
        self.upcast::<Node>()
            .GetParentNode()
            .and_then(DomRoot::downcast::<Element>)
    }

    /// Read an attribute from the target element.
    fn target_attribute(&self, target: &Element, name: &LocalName) -> Option<AttrValue> {
        target.with_attribute(&ns!(), name, |attr| attr.value().clone())
    }

    /// Read a string attribute from this element.
    fn string_attribute(&self, name: &LocalName) -> Option<DOMString> {
        self.upcast::<Element>()
            .with_attribute(&ns!(), name, |attr| attr.to_dom_string())
    }

    /// Rebuild the animation state from the element's attributes.
    fn rebuild_state(&self) {
        let element = self.upcast::<Element>();
        let mut state = AnimationState::new();
        state.attribute_name = self
            .string_attribute(&local_name!("attributeName"))
            .map(LocalName::from);
        state.from = self
            .string_attribute(&local_name!("from"))
            .and_then(|value| AnimatedValue::parse(&value.str()));
        state.to = self
            .string_attribute(&local_name!("to"))
            .and_then(|value| AnimatedValue::parse(&value.str()));
        state.by = self
            .string_attribute(&local_name!("by"))
            .and_then(|value| AnimatedValue::parse(&value.str()));
        state.values = self
            .string_attribute(&local_name!("values"))
            .and_then(|value| {
                let values: Vec<AnimatedValue> = value
                    .str()
                    .split(';')
                    .filter_map(AnimatedValue::parse)
                    .collect();
                if values.is_empty() {
                    None
                } else {
                    Some(values)
                }
            });
        state.dur = self
            .string_attribute(&local_name!("dur"))
            .and_then(|value| parse_clock_value(&value.str()));
        state.begin = self
            .string_attribute(&local_name!("begin"))
            .and_then(|value| parse_clock_value(&value.str()));
        state.repeat_count = self
            .string_attribute(&local_name!("repeatCount"))
            .and_then(|value| {
                let value = value.str();
                if value.trim() == "indefinite" {
                    Some(f64::INFINITY)
                } else {
                    value.trim().parse::<f64>().ok()
                }
            });
        state.fill = self
            .string_attribute(&local_name!("fill"))
            .map(|value| {
                if value.str().trim() == "freeze" {
                    FillMode::Freeze
                } else {
                    FillMode::Remove
                }
            })
            .unwrap_or(FillMode::Remove);
        state.calc_mode = self
            .string_attribute(&local_name!("calcMode"))
            .map(|value| CalcMode::parse(&value.str()))
            .unwrap_or(CalcMode::Linear);
        state.additive = self
            .string_attribute(&local_name!("additive"))
            .map(|value| {
                if value.str().trim() == "sum" {
                    AdditiveMode::Sum
                } else {
                    AdditiveMode::Replace
                }
            })
            .unwrap_or(AdditiveMode::Replace);
        state.accumulate = self
            .string_attribute(&local_name!("accumulate"))
            .is_some_and(|value| value.str().trim() == "sum");
        *self.state.borrow_mut() = state;
    }

    /// Restore the target attribute to its base value, undoing any animation
    /// that was applied.
    fn restore_base_value(&self) {
        let (attribute_name, base_value) = {
            let mut state = self.state.borrow_mut();
            if !state.applied {
                return;
            }
            state.applied = false;
            state.last_applied_time = None;
            let attribute_name = state.attribute_name.clone();
            let base_value = state.base_value.take();
            (attribute_name, base_value)
        };
        let (Some(attribute_name), Some(base_value)) = (attribute_name, base_value) else {
            return;
        };
        if let Some(target) = self.target() {
            let cx = self.upcast::<Node>().owner_doc().window().as_global().cx();
            target.set_attribute(cx, &attribute_name, base_value);
        }
    }

    /// Apply the animation at the given document time (in seconds).
    fn apply_at(&self, document_time: f64) {
        if !self.upcast::<Node>().is_connected() {
            return;
        }
        self.rebuild_state();
        let Some(target) = self.target() else {
            return;
        };

        // Compute the value that should be applied, if any.
        let Some((attribute_name, value)) = self.compute_value(document_time) else {
            // The animation is not contributing a value: restore the base value.
            self.restore_base_value();
            return;
        };

        // Remember the underlying value before the first application so that it
        // can be restored later, and so that `additive`/`accumulate` can use it.
        {
            let mut state = self.state.borrow_mut();
            if !state.applied {
                state.base_value = self.target_attribute(&target, &attribute_name);
                state.applied = true;
            }
            state.last_applied_time = Some(document_time);
        }

        let cx = self.upcast::<Node>().owner_doc().window().as_global().cx();
        target.set_attribute(cx, &attribute_name, value);
    }

    /// Compute the attribute name and value to apply at `document_time`.
    fn compute_value(&self, document_time: f64) -> Option<(LocalName, AttrValue)> {
        let state = self.state.borrow();
        let attribute_name = state.attribute_name.clone()?;
        let dur = state.dur?;
        if dur <= 0.0 {
            return None;
        }
        let begin = state.begin.unwrap_or(0.0);
        let elapsed = document_time - begin;
        if elapsed < 0.0 || !state.is_active_at(elapsed) {
            return None;
        }

        let total_duration = state.total_duration().unwrap_or(dur);
        let (progress, iteration) = if elapsed < total_duration {
            let iteration = (elapsed / dur) as usize;
            let iteration_start = iteration as f64 * dur;
            let iteration_progress = ((elapsed - iteration_start) / dur).clamp(0.0, 1.0);
            (iteration_progress, iteration)
        } else {
            (1.0, 0)
        };

        let value = if state.calc_mode == CalcMode::Discrete {
            let index = if elapsed < total_duration {
                ((elapsed / dur) as usize).min(
                    state
                        .values
                        .as_ref()
                        .map(|values| values.len().saturating_sub(1))
                        .unwrap_or(1),
                )
            } else {
                state
                    .values
                    .as_ref()
                    .map(|values| values.len().saturating_sub(1))
                    .unwrap_or(1)
            };
            state
                .values
                .as_ref()
                .and_then(|values| values.get(index).cloned())
                .or_else(|| state.to.clone())
                .or_else(|| state.from.clone())
        } else if let Some(values) = &state.values {
            if values.len() == 1 {
                values[0].clone()
            } else {
                let scaled = progress * (values.len() - 1) as f64;
                let index = (scaled as usize).min(values.len() - 2);
                let fraction = scaled - index as f64;
                AnimatedValue::interpolate(&values[index], &values[index + 1], fraction)
            }
        } else {
            match (&state.from, &state.to) {
                (Some(from), Some(to)) => AnimatedValue::interpolate(from, to, progress),
                (Some(from), None) => from.clone(),
                (None, Some(to)) => to.clone(),
                (None, None) => state.by.clone()?,
            }
        };

        // Account for `additive="sum"` and `accumulate="sum"`.
        let mut value = value;
        if state.additive == AdditiveMode::Sum {
            if let Some(base) = state
                .base_value
                .as_ref()
                .and_then(|base| AnimatedValue::from_base(&base.to_string()))
            {
                value = base.add(&value);
            }
        }
        if state.accumulate && iteration > 0 {
            if let Some(to) = &state.to {
                let repeated = (0..iteration).fold(to.clone(), |acc, _| acc.add(to));
                value = value.add(&repeated);
            }
        }

        Some((attribute_name, value.to_attr_value()))
    }

    /// Apply the animation for the current document timeline value.
    fn update(&self) {
        let document_time = self
            .upcast::<Node>()
            .owner_doc()
            .current_animation_timeline_value();
        self.apply_at(document_time);
    }
}

impl VirtualMethods for SVGAnimationElement {
    fn super_type(&self) -> Option<&dyn VirtualMethods> {
        Some(self.upcast::<SVGElement>() as &dyn VirtualMethods)
    }

    fn attribute_mutated(&self, cx: &mut JSContext, attr: AttrRef<'_>, mutation: AttributeMutation) {
        self.super_type()
            .unwrap()
            .attribute_mutated(cx, attr, mutation);
        // A change to any animation attribute invalidates the parsed state.
        self.restore_base_value();
        self.update();
    }

    fn children_changed(&self, cx: &mut JSContext, mutation: &crate::dom::node::ChildrenMutation) {
        self.super_type().unwrap().children_changed(cx, mutation);
        self.update();
    }

    fn bind_to_tree(&self, cx: &mut JSContext, context: &crate::dom::node::BindContext) {
        self.super_type().unwrap().bind_to_tree(cx, context);
        self.update();
    }
}